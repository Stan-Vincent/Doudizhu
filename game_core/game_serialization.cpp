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
    //QSet 转成 QVector<Card>,使用有序列表保证序列化确定性
    const CardList list = cards.toCardList(Cards::Asc);

    //告诉接收端牌数
    out << static_cast<quint8>(list.size());
    //逐张写出卡牌码
    for (const Card &card : list) {
        out << encodeCard(card);
    }
}

Cards readCards(QDataStream &in)
{
    // TODO(candidate): 读回 writeCards 格式；遇截断、异常或非法数据时安全处理。
    Cards cards;

    quint8 count = 0;
    in >> count;

    if (in.status() != QDataStream::Ok)
        return Cards();

    //两副牌的大小不超过54
    if (count > 55) {
        in.setStatus(QDataStream::ReadCorruptData);
        return Cards();
    }

    for (quint8 i = 0; i < count; ++i) {
        quint8 code = 0;
        in >> code;
        if (in.status() != QDataStream::Ok)
            break;

        cards.add(decodeCard(code));
    }
    return cards;
}

void writeCommand(QDataStream &out, const GameCommand &command)
{
    // TODO(candidate): 依次写 type、seat、bid 和 cards。

    out << static_cast<quint8>(command.type);
    out << static_cast<qint32>(command.seat);
    out << static_cast<qint32>(command.bid);
    writeCards(out, command.cards);
}

GameCommand readCommand(QDataStream &in)
{
    // TODO(candidate): 与 writeCommand 对应；损坏的头部必须返回可被引擎安全拒绝的命令。
    GameCommand command;

    command.seat = kInvalidSeat;
    quint8 typeVal = 0;
    qint32 seat = kInvalidSeat;
    qint32 bid = 0;

    in >> typeVal;
    if (in.status() != QDataStream::Ok)
        return command;

    in >> seat;
    if (in.status() != QDataStream::Ok)
        return command;

    in >> bid;
    if (in.status() != QDataStream::Ok)
        return command;

    Cards cards = readCards(in);
    if (in.status() != QDataStream::Ok)
        return command;

    //验证命令类型是否合法
    const GameCommandType type = static_cast<GameCommandType>(typeVal);
    if (type != GameCommandType::CallLord
        &&type != GameCommandType::PlayCards
        &&type != GameCommandType::Pass
        &&type != GameCommandType::ChaosVanish)
    {
        return command;
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

    out << static_cast<quint8>(event.type);
    out << static_cast<qint32>(event.seat);
    out << static_cast<qint32>(event.value);
    writeCards(out, event.cards);
}

GameEvent readEvent(QDataStream &in)
{
    // TODO(candidate): 与 writeEvent 对应；损坏的头部必须返回座位非法的默认事件。
    GameEvent event;

    event.seat = kInvalidSeat;
    quint8 typeVal = 0;
    qint32 seat = kInvalidSeat;
    qint32 value = 0;

    in >> typeVal;
    if (in.status() != QDataStream::Ok)
        return event;

    in >> seat;
    if (in.status() != QDataStream::Ok)
        return event;

    in >> value;
    if (in.status() != QDataStream::Ok)
        return event;

    Cards cards = readCards(in);
    if (in.status() != QDataStream::Ok)
        return event;

    // 验证事件类型是否合法
    const GameEventType type = static_cast<GameEventType>(typeVal);
    if (type != GameEventType::RoundStarted
        &&type != GameEventType::PrivateHandDealt
        &&type != GameEventType::TurnChanged
        &&type != GameEventType::BidAccepted
        &&type != GameEventType::RoundVoided
        &&type != GameEventType::LordSelected
        &&type != GameEventType::CardsPlayed
        &&type != GameEventType::PlayerPassed
        &&type != GameEventType::MultiplierChanged
        &&type != GameEventType::ScoreChanged
        &&type != GameEventType::RoundFinished
        &&type != GameEventType::TrickReset
        &&type != GameEventType::CardVanished)
    {
        return event;
    }

    event.type = type;
    event.seat = seat;
    event.value = value;
    event.cards = cards;
    return event;
}

} // namespace GameSerialization
