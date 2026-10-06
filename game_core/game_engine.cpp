#include "game_engine.h"
#include "deck.h"
#include "playhand.h"
#include <cstdio>
#include <QDebug>

const GameState &GameEngine::state() const
{
    return m_state;
}

CommandResult GameEngine::startRound(const CardList &deck, int firstSeat)
{
    // TODO(candidate): 前置校验牌堆和座位；发 3×17 张并保留 3 张底牌；
    // 进入 CallingLord，保留累计分数，并产出 RoundStarted、
    // PrivateHandDealt 和 TurnChanged 事件。

    if (!Deck::isValid(deck))
        return CommandResult::rejected(GameError::InvalidDeck);

    if (!isValidSeat(firstSeat))
        return CommandResult::rejected(GameError::InvalidSeat);

    //新局开始情况：1.首局开始(GamePhase::Waiting) 2.全员不叫流局后(GamePhase::Waiting) 3.RoundFinished（上局已打完）
    if (m_state.phase != GamePhase::Waiting && m_state.phase != GamePhase::RoundFinished)
        return CommandResult::rejected(GameError::InvalidPhase);

    //重置状态
    m_state.phase = GamePhase::CallingLord;
    m_state.currentSeat = firstSeat;
    m_state.highestBid = 0;
    m_state.highestBidder = kInvalidSeat;
    m_state.bidCount = 0;
    m_state.multiplier = 1;
    m_state.passCount = 0;
    m_state.winnerSeat = kInvalidSeat;
    //回合数加一
    m_state.roundNumber += 1;
    m_state.pendingCards.clear();
    m_state.pendingSeat = kInvalidSeat;
    m_state.playedCards.clear();
    m_state.vanishedCards.clear();
    m_state.bottomCards.clear();
    m_state.revealedBottomCards.clear();

    //重置player时保留 score和 connected
    for (auto &player : m_state.players) {
        player.hand.clear();
        player.role = PlayerRole::Unknown;
        player.handCount = 0;
        player.playsMade = 0;
    }

    //开始发牌 前 51 张轮流发给三人，最后 3 张为底牌
    for (int i = 0; i < deck.size(); ++i) {
        if (i < kPlayerCount * 17) {
            int seat = i % kPlayerCount;
            m_state.players[seat].hand.add(deck[i]);
        } else {
            m_state.bottomCards.add(deck[i]);
        }
    }
    for (auto &player : m_state.players) {
        player.handCount = player.hand.cardCount();
    }

    //前置校验牌堆和座位，构造CommandResult，赋值其成员
    CommandResult result;
    result.accepted = true;

    ///产出 RoundStarted、 PrivateHandDealt 和 TurnChanged 事件

    //GameEventType::RoundStarted中value代表roundNumber，roundNumber = event.value;
    result.events.append( {GameEventType::RoundStarted, firstSeat, m_state.roundNumber, Cards()} );

    for (int seat = 0; seat < kPlayerCount; ++seat) {

        result.events.append({GameEventType::PrivateHandDealt, seat, 0, m_state.players[seat].hand});
    }

    result.events.append({GameEventType::TurnChanged, firstSeat, 0, Cards()});

    return result;
}

CommandResult GameEngine::execute(const GameCommand &command)
{
    if (m_state.phase == GamePhase::RoundFinished)
        return CommandResult::rejected(GameError::RoundAlreadyFinished);

    CommandResult result;
    switch (command.type) {
    case GameCommandType::CallLord:
        result = executeCallLord(command);
        break;
    case GameCommandType::PlayCards:
        result = executePlayCards(command);
        break;
    case GameCommandType::Pass:
        result = executePass(command);
        break;
    case GameCommandType::ChaosVanish:
        result = executeChaosVanish(command);
        break;
    default:
        return CommandResult::rejected(GameError::InvalidPhase);
    }

    if (result.accepted) {
        const GameError vErr = validateState();
        if (vErr != GameError::None) {
            std::fprintf(stderr, "[Engine] validateState FAILED code=%d after cmd type=%d seat=%d\n",
                         static_cast<int>(vErr), static_cast<int>(command.type), command.seat);
            std::fflush(stderr);
        }
        Q_ASSERT(vErr == GameError::None);
    }
    return result;
}

CommandResult GameEngine::executeCallLord(const GameCommand &command)
{
    // TODO(candidate): 实现 0..3 叫分、严格抬价、叫 3 立即定地主，
    // 三人都不叫则 RoundVoided；所有拒绝必须无副作用。
    //前置检查
    if (m_state.phase != GamePhase::CallingLord)
        return CommandResult::rejected(GameError::InvalidPhase);
    if (!isValidSeat(command.seat))
        return CommandResult::rejected(GameError::InvalidSeat);
    if (command.seat != m_state.currentSeat)
        return CommandResult::rejected(GameError::NotCurrentPlayer);
    if (command.bid < 0 || command.bid > 3)
        return CommandResult::rejected(GameError::InvalidBid);

    //第一次叫分允许叫0（不叫），后续必须高于当前最高叫分，但是可以选择不叫（bid == 0）
    if (m_state.bidCount > 0 && command.bid != 0 && command.bid <= m_state.highestBid)
        return CommandResult::rejected(GameError::BidNotHighEnough);

    //执行叫分,此时才修改状态,bidCount记录已经叫了几次
    ++m_state.bidCount;
    //不叫(bid=0)只表示放弃本轮，不能覆盖之前的最高叫分
    //由于后续叫分已高于当前最高叫分，这时command.bid > 0必为最高分，更新当前状态
    if (command.bid > 0) {
        m_state.highestBid = command.bid;
        m_state.highestBidder = command.seat;
    }

    CommandResult result;
    result.accepted = true;
    //BidAccepted中value的含义为叫分
    result.events.append({GameEventType::BidAccepted, command.seat, command.bid, Cards()});

    // 叫3立即定地主selectLord
    if (command.bid == 3) {
        selectLord(command.seat, command.bid, &result.events);
        return result;
    }

    //所有人叫完都没叫三分
    if (m_state.bidCount >= kPlayerCount) {
        // 全员不叫 --> 流局，把已发出的牌全部收回，重置状态
        if (m_state.highestBid == 0) {

            m_state.phase = GamePhase::Waiting;
            m_state.currentSeat = kInvalidSeat;
            m_state.bidCount = 0;
            m_state.highestBid = 0;
            m_state.highestBidder = kInvalidSeat;
            m_state.roundNumber -= 1;   //回合数减一 流局不计入局数

            for (auto &player : m_state.players) {
                player.hand.clear();
                player.handCount = 0;
            }
            m_state.bottomCards.clear();
            m_state.revealedBottomCards.clear();
            m_state.vanishedCards.clear();
            result.events.append({GameEventType::RoundVoided, kInvalidSeat, 0, Cards()});
        }
        //有人叫分 --> 最高叫分者成为地主
        else
        {
            selectLord(m_state.highestBidder, m_state.highestBid, &result.events);
        }
        return result;
    }

    //否则轮到下一位叫分
    m_state.currentSeat = nextSeat(m_state.currentSeat);
    result.events.append({GameEventType::TurnChanged, m_state.currentSeat, 0, Cards()});
    return result;
}

CommandResult GameEngine::executePlayCards(const GameCommand &command)
{
    // TODO(candidate): 前置校验阶段、座位、牌权、牌型和压制关系；
    // 接受后更新手牌/pending/出牌记录，处理炸弹倍数与结算事件。

    if (m_state.phase != GamePhase::Playing)
        return CommandResult::rejected(GameError::InvalidPhase);
    if (!isValidSeat(command.seat))
        return CommandResult::rejected(GameError::InvalidSeat);
    if (command.seat != m_state.currentSeat)
        return CommandResult::rejected(GameError::NotCurrentPlayer);
    if (!m_state.players[command.seat].hand.contains(command.cards))
        return CommandResult::rejected(GameError::CardsNotOwned);

    // 识别牌型
    PlayHand playHand(command.cards);
    if (playHand.getHandType() == PlayHand::Hand_Unknown)
        return CommandResult::rejected(GameError::InvalidHand);
    //地主第一次不能出pass
    if (m_state.pendingCards.isEmpty() && playHand.getHandType() == PlayHand::Hand_Pass)
        return CommandResult::rejected(GameError::InvalidHand);

    // 检查是否能压过上家（若 pendingCards 非空）
    if (!m_state.pendingCards.isEmpty()) {
        PlayHand pendingHand(m_state.pendingCards);
        if (!playHand.canBeat(pendingHand))
            return CommandResult::rejected(GameError::HandDoesNotBeatPending);
    }

    // 执行出牌
    m_state.players[command.seat].hand.remove(command.cards);
    m_state.players[command.seat].handCount = m_state.players[command.seat].hand.cardCount();
    m_state.players[command.seat].playsMade++;
    m_state.playedCards.add(command.cards);
    m_state.pendingCards = command.cards;
    m_state.pendingSeat = command.seat;
    m_state.passCount = 0;

    CommandResult result;
    result.accepted = true;
    result.events.append({GameEventType::CardsPlayed, command.seat, 0, command.cards});

    // 炸弹/火箭翻倍（纯炸弹或纯王炸）
    if (playHand.getHandType() == PlayHand::Hand_Bomb ||
        playHand.getHandType() == PlayHand::Hand_Bomb_Jokers) {
        m_state.multiplier *= 2;
        result.events.append({GameEventType::MultiplierChanged, command.seat, m_state.multiplier, Cards()});
    }

    // 检查是否出完手牌，是则本局结束
    if (m_state.players[command.seat].hand.isEmpty()) {
        finishRound(command.seat, &result.events);
        return result;
    }

    // 否则轮到下一位
    m_state.currentSeat = nextSeat(m_state.currentSeat);
    result.events.append({GameEventType::TurnChanged, m_state.currentSeat, 0, Cards()});
    return result;
}

CommandResult GameEngine::executePass(const GameCommand &command)
{
    // TODO(candidate): 领出者不能过；连续两家过后清空 pending，
    // 主动权回到最后出牌者，并发出 TrickReset / TurnChanged。
    // 实际实现
    if (m_state.phase != GamePhase::Playing)
        return CommandResult::rejected(GameError::InvalidPhase);
    if (!isValidSeat(command.seat))
        return CommandResult::rejected(GameError::InvalidSeat);
    if (command.seat != m_state.currentSeat)
        return CommandResult::rejected(GameError::NotCurrentPlayer);
    if (m_state.pendingCards.isEmpty())
        return CommandResult::rejected(GameError::CannotPassWhenLeading);

    ++m_state.passCount;
    CommandResult result;
    result.accepted = true;
    result.events.append({GameEventType::PlayerPassed, command.seat, 0, Cards()});

    if (m_state.passCount >= 2) {
        // 连续两家过，主动权回到最后出牌者
        int leadSeat = m_state.pendingSeat;
        m_state.pendingCards.clear();
        m_state.pendingSeat = kInvalidSeat;
        m_state.passCount = 0;
        m_state.currentSeat = leadSeat;
        result.events.append({GameEventType::TrickReset, leadSeat, 0, Cards()});
        result.events.append({GameEventType::TurnChanged, leadSeat, 0, Cards()});
    } else {
        // 否则轮到下一位
        m_state.currentSeat = nextSeat(m_state.currentSeat);
        result.events.append({GameEventType::TurnChanged, m_state.currentSeat, 0, Cards()});
    }
    return result;
}

CommandResult GameEngine::executeChaosVanish(const GameCommand &command)
{
    // Chaos mode: remove the given cards from the seat's hand, out of turn.
    // Valid in CallingLord (deal-time disappearance) and Playing (idle timeout).
    if (m_state.phase != GamePhase::CallingLord && m_state.phase != GamePhase::Playing)
        return CommandResult::rejected(GameError::InvalidPhase);
    if (!isValidSeat(command.seat))
        return CommandResult::rejected(GameError::InvalidSeat);
    if (command.cards.isEmpty())
        return CommandResult::rejected(GameError::InvalidHand);
    // The cards must actually be in the seat's hand (the session picks from the
    // authoritative hand, but guard against stale picks / duplicates).
    if (!m_state.players[command.seat].hand.contains(command.cards))
        return CommandResult::rejected(GameError::CardsNotOwned);

    m_state.players[command.seat].hand.remove(command.cards);
    m_state.players[command.seat].handCount = m_state.players[command.seat].hand.cardCount();
    // Move the vanished cards to a side pile so the 54-card / no-duplicate
    // invariant in validateState() stays honest (the cards still exist, just
    // out of play). Without this the assert in execute() would fire.
    m_state.vanishedCards.add(command.cards);

    CommandResult result;
    result.accepted = true;
    result.events.append({GameEventType::CardVanished, command.seat, 0, command.cards});
    return result;
}


void GameEngine::selectLord(int seat, int bid, QVector<GameEvent> *events)
{
    // TODO(candidate): 设置 1 地主 + 2 农民；底牌归地主；进入 Playing；
    // 倍数初始化为叫分，并发出 LordSelected / TurnChanged。

    //分派身份 地主和农民
    m_state.players[seat].role = PlayerRole::Lord;
    for (int i = 0; i < kPlayerCount; ++i)
    {
        if (i != seat) {
            m_state.players[i].role = PlayerRole::Farmer;
        }
    }

    //底牌归地主
    m_state.players[seat].hand.add(m_state.bottomCards);
    m_state.players[seat].handCount = m_state.players[seat].hand.cardCount();
    //展示底牌
    m_state.revealedBottomCards = m_state.bottomCards;
    //底牌清空
    m_state.bottomCards.clear();

    // 进入出牌阶段
    m_state.phase = GamePhase::Playing;
    //倍数初始值为叫分
    m_state.multiplier = bid;
    //地主先出牌
    m_state.currentSeat = seat;
    m_state.passCount = 0;
    m_state.pendingCards.clear();
    m_state.pendingSeat = kInvalidSeat;

    // 发出事件
    //value --> 叫分
    events->append({GameEventType::LordSelected, seat, bid, m_state.revealedBottomCards});
    //value --> 当前倍数
    events->append({GameEventType::MultiplierChanged, seat, m_state.multiplier, Cards()});
    events->append({GameEventType::TurnChanged, seat, 0, Cards()});
}

void GameEngine::finishRound(int winnerSeat, QVector<GameEvent> *events)
{
    // TODO(candidate): 判断春天/反春天并翻倍，完成零和计分，
    // 进入 RoundFinished，并发出 MultiplierChanged、ScoreChanged、RoundFinished。

    m_state.winnerSeat = winnerSeat;
    m_state.phase = GamePhase::RoundFinished;

    // 找到地主座位
    int lordSeat = kInvalidSeat;
    for (int i = 0; i < kPlayerCount; ++i) {
        if (m_state.players[i].role == PlayerRole::Lord) {
            lordSeat = i;
            break;
        }
    }

    // 判断春天/反春天
    bool spring = false;
    // 地主赢 且 所有农民 playsMade == 0
    if (winnerSeat == lordSeat) {

        bool farmersNeverPlayed = true;
        for (int i = 0; i < kPlayerCount; ++i) {
            if (i != lordSeat && m_state.players[i].playsMade > 0) {
                farmersNeverPlayed = false;
                break;
            }
        }
        spring = farmersNeverPlayed;
    } else {
        // 反春天：农民获胜且地主只出过开局那一手,playsMade <= 1
        spring = (m_state.players[lordSeat].playsMade <= 1);
    }

    if (spring) {
        m_state.multiplier *= 2;
        events->append({GameEventType::MultiplierChanged, kInvalidSeat, m_state.multiplier, Cards()});
    }

    //计分
    int newScore = m_state.multiplier;
    //地主赢
    if (winnerSeat == lordSeat) {
        m_state.players[lordSeat].score += 2 * newScore;

        for (int i = 0; i < kPlayerCount; ++i) {
            if (i != lordSeat) {
                m_state.players[i].score -= newScore;
            }
        }
    }
    //农民赢
    else
    {
        m_state.players[lordSeat].score -= 2 * newScore;
        for (int i = 0; i < kPlayerCount; ++i) {
            if (i != lordSeat) {
                m_state.players[i].score += newScore;
            }
        }
    }

    // 发出 ScoreChanged 事件
    for (int i = 0; i < kPlayerCount; ++i) {
        //value --> 该座位的累计分
        events->append({GameEventType::ScoreChanged, i, m_state.players[i].score, Cards()});
    }

    // 发出 RoundFinished 事件
    events->append({GameEventType::RoundFinished, winnerSeat, 0, Cards()});
}

GameError GameEngine::validateState() const
{
    // TODO(candidate): 校验 54 张不重不漏、合法座位、角色 1+2、
    // pending 属于已出牌集合以及三家总分为 0 等不变量。
    // 初期可保留 None，避免内部断言阻塞其它模块的渐进实现。

    // 阶段合法性
    if (m_state.phase != GamePhase::Waiting &&
        m_state.phase != GamePhase::Dealing &&
        m_state.phase != GamePhase::CallingLord &&
        m_state.phase != GamePhase::Playing &&
        m_state.phase != GamePhase::RoundFinished)
        return GameError::InvalidPhase;

    // 座位合法性
    if (m_state.currentSeat != kInvalidSeat && !isValidSeat(m_state.currentSeat))
        return GameError::InvalidSeat;
    if (m_state.pendingSeat != kInvalidSeat && !isValidSeat(m_state.pendingSeat))
        return GameError::InvalidSeat;
    if (m_state.highestBidder != kInvalidSeat && !isValidSeat(m_state.highestBidder))
        return GameError::InvalidSeat;

    // 角色检查 只在Playing 或 RoundFinished 阶段检查
    if (m_state.phase == GamePhase::Playing || m_state.phase == GamePhase::RoundFinished) {
        int lordCount = 0, farmerCount = 0;
        for (const auto &p : m_state.players) {
            if (p.role == PlayerRole::Lord) lordCount++;
            else if (p.role == PlayerRole::Farmer) farmerCount++;
        }
        if (lordCount != 1 || farmerCount != 2)
            return GameError::InvalidHand;
    }

    //构建所有牌集合，同时统计"含重复"的总张数。
    //Cards 底层是 QSet，add() 会自动去重——因此"把所有牌并起来再排序
    //查相邻重复"永远查不出重复牌。唯一可靠的判据是：
    //各容器张数之和(含重复) != 并集张数(去重后)  =>  同一张牌被记在了两处。
    Cards allCards;
    int totalCount = 0;
    for (const auto &p : m_state.players) {
        allCards.add(p.hand);
        totalCount += p.hand.cardCount();
    }
    allCards.add(m_state.playedCards);
    totalCount += m_state.playedCards.cardCount();

    allCards.add(m_state.vanishedCards);
    totalCount += m_state.vanishedCards.cardCount();

    //CallingLord 阶段底牌尚未归地主，需要计入总数
    if (m_state.phase == GamePhase::CallingLord) {
        allCards.add(m_state.bottomCards);
        totalCount += m_state.bottomCards.cardCount();
    }

    //检查总牌数
    if (m_state.phase != GamePhase::Waiting) {
        if (allCards.cardCount() != 54)
            return GameError::InvalidDeck;
    }
    else {
        //Waiting 阶段没有任何牌
        if (allCards.cardCount() != 0)
            return GameError::InvalidDeck;
    }

    //检查无重复牌：张数对不上即存在重复
    if (totalCount != allCards.cardCount())
        return GameError::InvalidDeck;

    //检查 pendingCards 是否属于已出牌集合
    if (!m_state.pendingCards.isEmpty() && !m_state.playedCards.contains(m_state.pendingCards))
        return GameError::InvalidHand;

    //三家总分必须为 0
    int totalScore = 0;
    for (const auto &p : m_state.players)
        totalScore += p.score;
    if (totalScore != 0)
        return GameError::InvalidHand;

    //所有不变量都满足，状态健康
    return GameError::None;
}

GameState GameEngine::projectedStateFor(int seat) const
{
    // TODO(candidate): 非法座位返回空状态；合法座位只保留自己的手牌，
    // 并在叫地主结束前隐藏底牌。

    // 非法座位返回空状态
    if (!isValidSeat(seat))
        return GameState();

    //拷贝整个GameState，只清空需要隐藏的部分，其余保持原样
    GameState projected = m_state;

    // 隐藏其他玩家的手牌，仅保留 handCount
    for (int i = 0; i < kPlayerCount; ++i) {
        if (i != seat) {
            projected.players[i].hand.clear();
        }
    }

    // 叫地主阶段结束前隐藏底牌
    if (m_state.phase == GamePhase::CallingLord) {
        projected.bottomCards.clear();
        projected.revealedBottomCards.clear();
    }

    return projected;
}
