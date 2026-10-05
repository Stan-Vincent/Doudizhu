#include "local_session.h"
#include "deck.h"
#include "ai_policy.h"

#include <QTimer>
#include <QDebug>
#include <QRandomGenerator>
#include <random>

namespace {
// AI 每步行动前的延迟（毫秒）。给玩家留出看清对手出牌的时间。
constexpr int kAIMoveDelayMs = 1200;
}

LocalSession::LocalSession(QObject *parent)
    : QObject(parent)
    , m_aiEnabled(true)
{
    // Default: all AI players
    for (int i = 0; i < kPlayerCount; ++i) {
        m_playerTypes[i] = PlayerType::AI;
    }
}

LocalSession::~LocalSession()
{
}

void LocalSession::setPlayerType(int seat, PlayerType type)
{
    if (!isValidSeat(seat))
        return;

    m_playerTypes[seat] = type;
}

PlayerType LocalSession::playerType(int seat) const
{
    if (!isValidSeat(seat))
        return PlayerType::User;
    return m_playerTypes[seat];
}

bool LocalSession::startRound(quint32 seed)
{
    // TODO(candidate): 生成或接收种子，调用 Deck::shuffled 和 m_engine.startRound，
    // 转发成功事件，并在适用时接入已给出的 chaos 发牌逻辑和 AI 调度。

    qDebug() << "[LocalSession] startRound called, seed =" << seed;   /// 新增

    // 1. 生成种子（0 表示随机）
    if (seed == 0) {
        seed = QRandomGenerator::global()->generate();
    }
    qDebug() << "[LocalSession] effective seed =" << seed;            /// 新增

    // 2. 确定性选择第一个叫地主的座位：按局数轮换，首局固定为座位 0。
    //    不能随机——GameControlAdapter 把座位 0 固定映射为人类玩家，relay 服务器的
    //    权威开局也固定 firstSeat=0；随机会导致人类玩家的叫分/出牌命令随机被
    //    NotCurrentPlayer 拒绝（local_session_tests 的 "Session accepts user
    //    commands" 间歇性失败正是这个原因）。
    const int firstSeat = m_engine.state().roundNumber % kPlayerCount;
    qDebug() << "[LocalSession] firstSeat =" << firstSeat;            /// 新增

    // 3. 洗牌并发牌
    const CardList deck = Deck::shuffled(seed);
    CommandResult result = m_engine.startRound(deck, firstSeat);
    qDebug() << "[LocalSession] engine.startRound accepted =" << result.accepted
             << "error =" << static_cast<int>(result.error);          /// 新增

    if (!result.accepted) {
        qWarning() << "LocalSession::startRound: engine rejected startRound"
                   << static_cast<int>(result.error);
        return false;
    }

    // 4. 转发发牌事件
    qDebug() << "[LocalSession] events count =" << result.events.size();   /// 新增
    processEvents(result.events);

    // 5. 混沌模式：发牌后随机消牌
    if (m_chaosActive && m_chaosDisappearOnDraw) {
        for (int seat = 0; seat < kPlayerCount; ++seat) {
            maybeVanishOnDeal(seat);
        }
    }

    // 6. 若当前座位是 AI，启动 AI 决策
    const int current = m_engine.state().currentSeat;
    qDebug() << "[LocalSession] after startRound phase ="
             << static_cast<int>(m_engine.state().phase)
             << "currentSeat =" << current;                             /// 新增

    if (isValidSeat(current) && m_playerTypes[current] == PlayerType::AI) {
        scheduleAIMove();
    }

    return true;
}

CommandResult LocalSession::executeCommand(const GameCommand &command)
{
    // TODO(candidate): 拒绝外部控制 AI 座位；其余命令交给引擎，
    // 接受后转发事件并继续调度 AI。
    (void)command;

    qDebug() << "[LocalSession] executeCommand called, type =" << static_cast<int>(command.type)
             << "seat =" << command.seat << "bid =" << command.bid;   // 新增

    // 1. 外部只能控制 User 座位，且必须是当前行动座位
    const int seat = command.seat;
    if (!isValidSeat(seat))
        return CommandResult::rejected(GameError::InvalidSeat);

    if (m_playerTypes[seat] != PlayerType::User)
        return CommandResult::rejected(GameError::InvalidPhase); // 不能控制 AI

    if (m_engine.state().currentSeat != seat)
        return CommandResult::rejected(GameError::NotCurrentPlayer);

    // 2. 提交给引擎
    CommandResult result = m_engine.execute(command);

    // 3. 若接受，转发事件并继续调度
    if (result.accepted) {
        processEvents(result.events);

        // 检查当前是否轮到 AI
        const int nextSeat = m_engine.state().currentSeat;
        if (isValidSeat(nextSeat) && m_playerTypes[nextSeat] == PlayerType::AI) {
            scheduleAIMove();
        }
    }

    return result;
}

GamePhase LocalSession::currentPhase() const
{
    return m_engine.state().phase;
}

int LocalSession::currentSeat() const
{
    return m_engine.state().currentSeat;
}

PlayerType LocalSession::currentPlayerType() const
{
    const int seat = currentSeat();
    if (!isValidSeat(seat))
        return PlayerType::User;
    return m_playerTypes[seat];
}

GameState LocalSession::projectedStateFor(int seat) const
{
    return m_engine.projectedStateFor(seat);
}

const GameState &LocalSession::fullState() const
{
    return m_engine.state();
}

void LocalSession::setAIEnabled(bool enabled)
{
    m_aiEnabled = enabled;
}

void LocalSession::triggerAIMove()
{
    const int seat = currentSeat();
    if (!isValidSeat(seat))
        return;

    if (m_playerTypes[seat] == PlayerType::AI) {
        executeAIMove(seat);
    }
}

void LocalSession::processEvents(const QVector<GameEvent> &events)
{
    // TODO(candidate): 逐个 emit eventEmitted；RoundFinished 额外 emit
    // roundFinished；RoundVoided 应延迟重新发牌，避免全员不叫后卡住。
    (void)events;

    for (const GameEvent &event : events) {
        if (event.type == GameEventType::RoundFinished) {
            // 延迟发送回合结束事件，避免 UI 在出牌动画未结束时清理动画目标
            const int winnerSeat = event.seat;
            QTimer::singleShot(500, this, [this, winnerSeat]() {
                emit eventEmitted(GameEvent{GameEventType::RoundFinished, winnerSeat, 0, Cards()});
                emit roundFinished(winnerSeat);
            });
            continue;
        }

        emit eventEmitted(event);

        if (event.type == GameEventType::RoundVoided) {
            QTimer::singleShot(0, this, [this]() {
                startRound(0);
            });
        }
    }
}

void LocalSession::scheduleAIMove()
{
    // TODO(candidate): 仅在 CallingLord / Playing 阶段且当前座位为 AI 时，
    // 用 QTimer 延迟 kAIMoveDelayMs 后调用 executeAIMove。

    // 只在 AI 启用且当前座位是 AI 时调度
    if (!m_aiEnabled)
        return;

    const int seat = currentSeat();
    if (!isValidSeat(seat) || m_playerTypes[seat] != PlayerType::AI)
        return;

    const GamePhase phase = currentPhase();
    if (phase != GamePhase::CallingLord && phase != GamePhase::Playing)
        return;

    // 延迟后执行 AI 行动
    QTimer::singleShot(kAIMoveDelayMs, this, [this, seat]() {
        // 再次检查条件，防止期间状态变化
        if (currentSeat() == seat &&
            m_playerTypes[seat] == PlayerType::AI &&
            (currentPhase() == GamePhase::CallingLord ||
             currentPhase() == GamePhase::Playing)) {
            executeAIMove(seat);
        }
    });
}

void LocalSession::executeAIMove(int seat)
{
    // TODO(candidate): 调用 AIPolicy::performMove 执行一步 AI，
    // 接受后转发事件并继续调度；可保留与已给 chaos 逻辑的衔接。
    (void)seat;

    if (!isValidSeat(seat) || m_playerTypes[seat] != PlayerType::AI)
        return;

    // 调用 AIPolicy 执行一步（包含决策和兜底）
    CommandResult result = AIPolicy::performMove(m_engine, seat);

    if (result.accepted) {
        processEvents(result.events);

        // 继续调度下一个 AI（如果轮到）
        const int nextSeat = currentSeat();
        if (isValidSeat(nextSeat) && m_playerTypes[nextSeat] == PlayerType::AI) {
            scheduleAIMove();
        }
    } else {
        qWarning() << "LocalSession::executeAIMove: AIPolicy move failed for seat"
                   << seat << "error" << static_cast<int>(result.error);
        // 理论上 AIPolicy 内部已兜底，不应到达此处；若到达，则尝试强制过牌避免卡死
        if (currentPhase() == GamePhase::Playing) {
            const CommandResult passResult = m_engine.execute(GameCommand::pass(seat));
            if (passResult.accepted) {
                processEvents(passResult.events);
                const int nextSeat = currentSeat();
                if (isValidSeat(nextSeat) && m_playerTypes[nextSeat] == PlayerType::AI) {
                    scheduleAIMove();
                }
            }
        }
    }
}

void LocalSession::setChaosParams(bool active, double disappearChance,
                                  double transformChance, bool disappearOnDraw,
                                  bool transformOnPlay, bool disappearOnIdle)
{
    m_chaosActive = active;
    m_chaosDisappearChance = disappearChance;
    m_chaosTransformChance = transformChance;
    m_chaosDisappearOnDraw = disappearOnDraw;
    m_chaosTransformOnPlay = transformOnPlay;
    m_chaosDisappearOnIdle = disappearOnIdle;
}

bool LocalSession::rollChaos(double chance) const
{
    if (chance <= 0.0) return false;
    if (chance >= 1.0) return true;
    return QRandomGenerator::global()->bounded(10000) < static_cast<int>(chance * 10000);
}

bool LocalSession::vanishRandomCard(int seat)
{
    if (!m_chaosActive || !isValidSeat(seat))
        return false;

    const CardList list = m_engine.state().players[seat].hand.toCardList(Cards::NoSort);
    if (list.isEmpty())
        return false;

    const Card victim = list[QRandomGenerator::global()->bounded(list.size())];
    Cards toVanish;
    toVanish.add(victim);

    const CommandResult result = m_engine.execute(GameCommand::chaosVanish(seat, toVanish));
    if (result.accepted) {
        processEvents(result.events);
        return true;
    }
    return false;
}

bool LocalSession::tryIdleVanish(int seat)
{
    if (!m_chaosActive || !m_chaosDisappearOnIdle || !isValidSeat(seat))
        return false;
    if (!rollChaos(m_chaosDisappearChance))
        return false;
    return vanishRandomCard(seat);
}

void LocalSession::maybeVanishOnDeal(int seat)
{
    if (!isValidSeat(seat))
        return;

    const CardList list = m_engine.state().players[seat].hand.toCardList(Cards::NoSort);
    Cards toVanish;
    for (const Card &c : list) {
        // Independent per-card roll (the onDraw disappearance rule).
        if (rollChaos(m_chaosDisappearChance))
            toVanish.add(c);
    }
    if (toVanish.isEmpty())
        return;

    const CommandResult result = m_engine.execute(GameCommand::chaosVanish(seat, toVanish));
    if (result.accepted)
        processEvents(result.events);
}

Cards LocalSession::transformPlayerPlay(const Cards &intended)
{
    // Player shares the same authoritative transform as the AI. The human is the
    // current seat when this runs (called from onBtnPlay on the player's turn);
    // the transformed cards are then played as a normal command (executeCommand
    // does not re-transform, so no double-apply).
    return maybeTransformPlay(currentSeat(), intended);
}

Cards LocalSession::maybeTransformPlay(int seat, const Cards &intended)
{
    if (!m_chaosActive || !m_chaosTransformOnPlay || intended.isEmpty()
        || !isValidSeat(seat))
        return intended;

    if (!rollChaos(m_chaosTransformChance))
        return intended;

    // Pick a random played card to mutate, and a random replacement from the
    // rest of the hand (not already among the played cards).
    CardList outList = intended.toCardList(Cards::NoSort);
    const CardList hand = m_engine.state().players[seat].hand.toCardList(Cards::NoSort);
    if (outList.isEmpty() || hand.isEmpty())
        return intended;

    const int outIdx = QRandomGenerator::global()->bounded(outList.size());
    const Card from = outList[outIdx];

    Card to;
    bool found = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const Card cand = hand[QRandomGenerator::global()->bounded(hand.size())];
        if (!intended.contains(cand)) {
            to = cand;
            found = true;
            break;
        }
    }
    if (!found)
        return intended;

    outList[outIdx] = to;
    Cards result;
    for (const Card &c : outList)
        result.add(c);

    emit chaosCardsTransformed(seat, from, to);
    return result;
}
