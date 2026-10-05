#include "game_serialization.h"

namespace GameSerialization
{

[[maybe_unused]] static quint8 encodeCard(const Card &card)
{
    return static_cast<quint8>(card.getpoint() * 10 + card.getsuit());
}

[[maybe_unused]] static Card decodeCard(quint8 code)
{
    const int point = code / 10;
    const int suit = code % 10;

    const bool joker = (point == Card::Card_SJ || point == Card::Card_BJ)
                       && suit == Card::Suit_Begin;
    const bool normal = (point >= Card::Card_3 && point <= Card::Card_2)
                        && (suit >= Card::Diamond && suit <= Card::Spade);
    if (!joker && !normal)
        return Card();

    return Card(static_cast<Card::CardPoint>(point),
                static_cast<Card::CardSuit>(suit));
}

void writeCards(QDataStream &out, const Cards &cards)
{
    // TODO(candidate): 写出 quint8 数量，再逐张写出卡牌码。
    (void)out;
    (void)cards;

    // 使用有序列表保证序列化确定性
    const CardList list = cards.toCardList(Cards::Asc);
    out << static_cast<quint8>(list.size());
    for (const Card &card : list) {
        out << encodeCard(card);
    }
}

Cards readCards(QDataStream &in)
{
    // TODO(candidate): 读回 writeCards 格式；遇截断、异常或非法数据时安全处理。
    (void)in;
    Cards cards;
    quint8 count = 0;
    in >> count;
    if (in.status() != QDataStream::Ok)
        return Cards();

    // 限制最大数量，避免恶意数据导致内存耗尽
    if (count > 108) {  // 两副牌的大小，实际不超过54，这里放宽
        in.setStatus(QDataStream::ReadCorruptData);
        return Cards();
    }

    for (quint8 i = 0; i < count; ++i) {
        quint8 code = 0;
        in >> code;
        // 流耗尽（截断）即停：返回已成功读到的牌，既不丢弃也不补默认牌
        // （见 game_serialization_tests 的截断用例：声称5张只给1张 → 读回1张）。
        if (in.status() != QDataStream::Ok)
            break;

        // 非法卡牌码统一解码为哨兵牌（Card_Begin/Suit_Begin）并照常入集，
        // 绝不产生越界枚举值（见 game_serialization_tests 的 N4 用例）。
        cards.add(decodeCard(code));
    }
    return cards;
}

void writeCommand(QDataStream &out, const GameCommand &command)
{
    // TODO(candidate): 依次写 type、seat、bid 和 cards。
    (void)out;
    (void)command;

    out << static_cast<quint8>(command.type);
    out << static_cast<qint32>(command.seat);
    out << static_cast<qint32>(command.bid);
    writeCards(out, command.cards);
}

GameCommand readCommand(QDataStream &in)
{
    // TODO(candidate): 与 writeCommand 对应；损坏的头部必须返回可被引擎安全拒绝的命令。
    (void)in;
    GameCommand command;
    command.seat = kInvalidSeat;  // 默认非法座位，保证引擎安全拒绝

    quint8 typeVal = 0;
    qint32 seat = kInvalidSeat;
    qint32 bid = 0;

    in >> typeVal;
    if (in.status() != QDataStream::Ok) return command;
    in >> seat;
    if (in.status() != QDataStream::Ok) return command;
    in >> bid;
    if (in.status() != QDataStream::Ok) return command;
    Cards cards = readCards(in);
    if (in.status() != QDataStream::Ok) return command;

    // 验证命令类型是否合法
    const GameCommandType type = static_cast<GameCommandType>(typeVal);
    if (type != GameCommandType::CallLord &&
        type != GameCommandType::PlayCards &&
        type != GameCommandType::Pass &&
        type != GameCommandType::ChaosVanish) {
        return command;  // 保留 seat 为 kInvalidSeat
    }

    command.type = type;
    command.seat = seat;
    command.bid = bid;
    command.cards = cards;
    return command;
}

void writeEvent(QDataStream &out, const GameEvent &event)
{
    // TODO(candidate): 依次写 type、seat、value 和 cards。
    (void)out;
    (void)event;

    out << static_cast<quint8>(event.type);
    out << static_cast<qint32>(event.seat);
    out << static_cast<qint32>(event.value);
    writeCards(out, event.cards);
}

GameEvent readEvent(QDataStream &in)
{
    // TODO(candidate): 与 writeEvent 对应；损坏的头部必须返回座位非法的默认事件。
    (void)in;
    GameEvent event;
    event.seat = kInvalidSeat;  // 默认非法座位

    quint8 typeVal = 0;
    qint32 seat = kInvalidSeat;
    qint32 value = 0;

    in >> typeVal;
    if (in.status() != QDataStream::Ok) return event;
    in >> seat;
    if (in.status() != QDataStream::Ok) return event;
    in >> value;
    if (in.status() != QDataStream::Ok) return event;
    Cards cards = readCards(in);
    if (in.status() != QDataStream::Ok) return event;

    // 验证事件类型是否合法
    const GameEventType type = static_cast<GameEventType>(typeVal);
    if (type != GameEventType::RoundStarted &&
        type != GameEventType::PrivateHandDealt &&
        type != GameEventType::TurnChanged &&
        type != GameEventType::BidAccepted &&
        type != GameEventType::RoundVoided &&
        type != GameEventType::LordSelected &&
        type != GameEventType::CardsPlayed &&
        type != GameEventType::PlayerPassed &&
        type != GameEventType::MultiplierChanged &&
        type != GameEventType::ScoreChanged &&
        type != GameEventType::RoundFinished &&
        type != GameEventType::TrickReset &&
        type != GameEventType::CardVanished) {
        return event;  // seat 保持 kInvalidSeat
    }

    event.type = type;
    event.seat = seat;
    event.value = value;
    event.cards = cards;
    return event;
}

} // namespace GameSerialization
