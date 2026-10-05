#include "deck.h"

#include "cards.h"

#include <algorithm>
#include <random>

CardList Deck::standard()
{
    // TODO(candidate): 构造标准 54 张牌。
    CardList cards;
    cards.reserve(54);

    //除了大小王以外的普通牌：3点~2点，每种点数4种花色
    for (int _pt = Card::Card_3; _pt <= Card::Card_2; ++_pt) {
        Card::CardPoint point = static_cast<Card::CardPoint>(_pt);
        cards.append(Card(point, Card::Diamond));
        cards.append(Card(point, Card::Club));
        cards.append(Card(point, Card::Heart));
        cards.append(Card(point, Card::Spade));
    }

    //王：无花色，设为Suit_Begin
    cards.append(Card(Card::Card_SJ, Card::Suit_Begin));
    cards.append(Card(Card::Card_BJ, Card::Suit_Begin));

    return cards;
}

CardList Deck::shuffled(quint32 seed)
{
    // TODO(candidate): 以 seed 播种，对 standard() 结果进行确定性洗牌。

    CardList cards = standard();
    //创建一个随机数生成器对象
    std::mt19937 rng(static_cast<unsigned int>(seed));
    std::shuffle(cards.begin(), cards.end(), rng);
    return cards;
}

bool Deck::isValid(const CardList &cards)
{
    // TODO(candidate): 校验 cards 是否恰好是一副无重复、无非法牌面的标准牌。
    (void)cards;
    if (cards.size() != 54)
        return false;

    //备份检查
    CardList ckCards = cards;
    //按照lessSort排序
    std::sort(ckCards.begin(), ckCards.end(), lessSort);

    //检查是否有重复
    for (int i = 1; i < ckCards.size(); ++i) {
        if (ckCards[i] == ckCards[i - 1])
            return false;
    }

    //检查每张牌的点数和花色合法性
    for (const Card &_card : cards) {
        Card::CardPoint _pt = _card.getpoint();
        Card::CardSuit _suit = _card.getsuit();

        //点数必须在Card_3 ~ Card_BJ之间
        if (_pt <= Card::Card_Begin || _pt >= Card::Card_End)
            return false;
        if (_pt < Card::Card_3 || _pt > Card::Card_BJ)
            return false;

        //王的花色为Suit_Begin
        if (_pt == Card::Card_SJ || _pt == Card::Card_BJ) {
            if (_suit != Card::Suit_Begin)
                return false;
        }
        //普通牌的花色是四种花色之一
        else {
            if (_suit <= Card::Suit_Begin || _suit >= Card::Suit_End)
                return false;
            if (_suit != Card::Diamond && _suit != Card::Club &&
                _suit != Card::Heart && _suit != Card::Spade)
                return false;
        }
    }

    //统计王的数量必须各一张
    bool hasSJ = false, hasBJ = false;
    for (const Card &card : cards) {
        if (card.getpoint() == Card::Card_SJ) hasSJ = true;
        if (card.getpoint() == Card::Card_BJ) hasBJ = true;
    }
    if (!hasSJ || !hasBJ)
        return false;

    return true;
}
