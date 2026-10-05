#include "playhand.h"
#include <QMap>
#include <algorithm>

///playhand.cpp —— 牌型识别与大小比较

//当前 .cpp 文件私有的全局辅助函数 (TODO)
namespace{
    //该函数判断点数是否在 连续的牌型（顺子、连队） 允许范围内（3~A）
    bool isInSequenceRange(Card::CardPoint p)
    {
        return p >= Card::Card_3 && p <= Card::Card_A;
    }

    //该函数判断点数列表是否连续
    bool isContinuous(const QVector<Card::CardPoint> &points)
    {
        //点数类型小于2一定连续
        if (points.size() < 2)
            return true;

        for (int i = 1; i < points.size(); ++i) {
            // 判断是否点数连续
            if (static_cast<int>(points[i]) != static_cast<int>(points[i-1]) + 1)
                return false;
        }
        return true;
    }

    //该函数判断列表是否只包含大小王各一张
    bool containsBothJokers(const QVector<Card::CardPoint> &points)
    {
        bool hasSJ = false, hasBJ = false;
        for (Card::CardPoint p : points) {
            if (p == Card::Card_SJ)
                hasSJ = true;
            else if (p == Card::Card_BJ)
                hasBJ = true;
        }
        return hasSJ && hasBJ;
    }
}

PlayHand::PlayHand()
    : m_type(Hand_Unknown), m_pt(Card::Card_Begin), m_extra(0)
{
}

PlayHand::PlayHand(const Cards &cards)
    : m_type(Hand_Unknown), m_pt(Card::Card_Begin), m_extra(0)
{
    classify(cards);
    judgeCardType();
}

PlayHand::PlayHand(HandType type, Card::CardPoint pt, int extra)
    : m_type(type), m_pt(pt), m_extra(extra)
{
}

PlayHand::HandType PlayHand::getHandType() const { return m_type; }
Card::CardPoint PlayHand::getCardPoint() const { return m_pt; }
int PlayHand::getExtra() const { return m_extra; }

//按点数出现张数分成 1/2/3/4 张四组
void PlayHand::classify(const Cards &cards)
{
    // TODO(candidate): 按点数统计张数，分别填充并排序
    // m_oneCard / m_twoCard / m_threeCard / m_fourCard。
    m_oneCard.clear();
    m_twoCard.clear();
    m_threeCard.clear();
    m_fourCard.clear();

    ///从最小牌 3 遍历到最大牌 大王 所有点数，按出现张数分配到对应容器 (TODO)
    for (int _pt = Card::Card_3; _pt <= Card::Card_BJ; ++_pt) {
        Card::CardPoint point = static_cast<Card::CardPoint>(_pt);

        //寻找该点数在cards中的对应的张数
        int count = cards.pointCount(point);

        switch (count)  //记录对应点数和张数
        {
            case 1: m_oneCard.append(point); break;
            case 2: m_twoCard.append(point); break;
            case 3: m_threeCard.append(point); break;
            case 4: m_fourCard.append(point); break;
            default: break; //点数不匹跳过
        }
    }
}

void PlayHand::judgeCardType()
{
    // TODO(candidate): 重置状态，并按正确优先级调用各个 isXxx()；命中后立即返回。
    //按优先级依次匹配牌型

    m_type = Hand_Unknown;
    m_pt = Card::Card_Begin;
    m_extra = 0;

    //通过按优先级调用函数判断牌型
    //都是通过牌的各种点数的数量来判断，判断后还要赋值基本信息
    if (isBombJokers()) return;
    if (isBombJokersSingle()) return;
    if (isBombJokersPair()) return;
    if (isBombJokersTwoSingle()) return;
    if (isBomb()) return;
    if (isBombSingle()) return;
    if (isBombPair()) return;
    if (isBombTwoSingle()) return;
    if (isPass()) return;
    if (isSingle()) return;
    if (isPair()) return;
    if (isTriple()) return;
    if (isTripleSingle()) return;
    if (isTriplePair()) return;
    if (isPlane()) return;
    if (isPlaneTwoSingle()) return;
    if (isPlaneTwoPair()) return;
    if (isSeqPair()) return;
    if (isSeqSingle()) return;

    // 无法识别
    m_type = Hand_Unknown;

}

bool PlayHand::isPass() { /* TODO(candidate) */
    //无记录牌 -> pass
    if (m_oneCard.isEmpty() && m_twoCard.isEmpty() && m_threeCard.isEmpty() && m_fourCard.isEmpty())
    {
        m_type = Hand_Pass;
        m_pt = Card::Card_Begin;
        m_extra = 0;
        return true;
    }
    return false;
}
bool PlayHand::isSingle() { /* TODO(candidate) */
    if (m_oneCard.size() == 1 && m_twoCard.isEmpty() && m_threeCard.isEmpty() && m_fourCard.isEmpty())
    {
        m_type = Hand_Single;
        m_pt = m_oneCard.first();
        m_extra = 1;
        return true;
    }
    return false;
}
bool PlayHand::isPair() { /* TODO(candidate) */
    if (m_twoCard.size() == 1 && m_oneCard.isEmpty() && m_threeCard.isEmpty() && m_fourCard.isEmpty())
    {
        m_type = Hand_Pair;
        m_pt = m_twoCard.first();
        m_extra = 1;
        return true;
    }
    return false;
}
bool PlayHand::isTriple() { /* TODO(candidate) */
    if (m_threeCard.size() == 1 && m_oneCard.isEmpty() && m_twoCard.isEmpty() && m_fourCard.isEmpty())
    {
        m_type = Hand_Triple;
        m_pt = m_threeCard.first();
        m_extra = 1;
        return true;
    }
    return false;
}
bool PlayHand::isTripleSingle() { /* TODO(candidate) */
    if (m_threeCard.size() == 1 && m_oneCard.size() == 1 && m_twoCard.isEmpty() && m_fourCard.isEmpty())
    {
        m_type = Hand_Triple_Single;
        m_pt = m_threeCard.first();
        m_extra = 1;
        return true;
    }
    return false;
}
bool PlayHand::isTriplePair() { /* TODO(candidate) */
    if (m_threeCard.size() == 1 && m_twoCard.size() == 1 && m_oneCard.isEmpty() && m_fourCard.isEmpty())
    {
        m_type = Hand_Triple_Pair;
        m_pt = m_threeCard.first();
        m_extra = 1;
        return true;
    }
    return false;
}
bool PlayHand::isPlane() { /* TODO(candidate) */
    if (m_threeCard.size() >= 2 && m_oneCard.isEmpty() && m_twoCard.isEmpty() && m_fourCard.isEmpty())
    {
        // 所有三张点数必须在 3~A 且连续
        foreach (Card::CardPoint p , m_threeCard) {
            if (!isInSequenceRange(p))
                return false;
        }
        if (isContinuous(m_threeCard)) {
            m_type = Hand_Plane;
            m_pt = m_threeCard.first();
            m_extra = m_threeCard.size();
            return true;
        }
    }
    return false;
}
bool PlayHand::isPlaneTwoSingle() { /* TODO(candidate) */
    if (m_threeCard.size() >= 2 && m_oneCard.size() == m_threeCard.size() && m_twoCard.isEmpty() && m_fourCard.isEmpty())
    {
        foreach (Card::CardPoint p , m_threeCard) {
            if (!isInSequenceRange(p))
                return false;
        }
        if (isContinuous(m_threeCard)) {
            m_type = Hand_Plane_Two_Single;
            m_pt = m_threeCard.first();
            m_extra = m_threeCard.size();
            return true;
        }
    }
    return false;
}
bool PlayHand::isPlaneTwoPair() { /* TODO(candidate) */
    if (m_threeCard.size() >= 2 && m_twoCard.size() == m_threeCard.size() && m_oneCard.isEmpty() && m_fourCard.isEmpty())
    {
        foreach (Card::CardPoint p , m_threeCard) {
            if (!isInSequenceRange(p))
                return false;
        }
        if (isContinuous(m_threeCard)) {
            m_type = Hand_Plane_Two_Pair;
            m_pt = m_threeCard.first();
            m_extra = m_threeCard.size();
            return true;
        }
    }
    return false;
}
bool PlayHand::isSeqPair() { /* TODO(candidate) */
    if (m_twoCard.size() >= 3 && m_oneCard.isEmpty() && m_threeCard.isEmpty() && m_fourCard.isEmpty())
    {
        foreach (Card::CardPoint p , m_twoCard) {
            if (!isInSequenceRange(p))
                return false;
        }
        if (isContinuous(m_twoCard)) {
            m_type = Hand_Seq_Pair;
            m_pt = m_twoCard.first();
            m_extra = m_twoCard.size();
            return true;
        }
    }
    return false;
}
bool PlayHand::isSeqSingle() { /* TODO(candidate) */
    if (m_oneCard.size() >= 5 && m_twoCard.isEmpty() && m_threeCard.isEmpty() && m_fourCard.isEmpty())
    {
        foreach (Card::CardPoint p , m_oneCard) {
            if (!isInSequenceRange(p))
                return false;
        }
        if (isContinuous(m_oneCard)) {
            m_type = Hand_Seq_Single;
            m_pt = m_oneCard.first();
            m_extra = m_oneCard.size();
            return true;
        }
    }

    return false;
}
bool PlayHand::isBomb() { /* TODO(candidate) */
    if (m_fourCard.size() == 1 && m_oneCard.isEmpty() && m_twoCard.isEmpty() && m_threeCard.isEmpty())
    {
        m_type = Hand_Bomb;
        m_pt = m_fourCard.first();
        m_extra = 1;
        return true;
    }
    return false;
}
bool PlayHand::isBombSingle() { /* TODO(candidate) */
    // 四带二（炸弹带两张单牌，共6张），4+1单（5张）不是合法斗地主牌型
    if (m_fourCard.size() == 1 && m_oneCard.size() == 2 &&
        m_twoCard.isEmpty() && m_threeCard.isEmpty())
    {
        m_type = Hand_Bomb_Single;
        m_pt = m_fourCard.first();
        m_extra = 2;
        return true;
    }
    return false;
}
bool PlayHand::isBombPair() { /* TODO(candidate) */
    // 四带两对（炸弹带两对，共8张）
    if (m_fourCard.size() == 1 && m_twoCard.size() == 2 &&
        m_oneCard.isEmpty() && m_threeCard.isEmpty())
    {
        m_type = Hand_Bomb_Pair;
        m_pt = m_fourCard.first();
        m_extra = 2;
        return true;
    }
    return false;
}
bool PlayHand::isBombTwoSingle() { /* TODO(candidate) */
    // 四带二的另一种合法带法：炸弹带一对（共6张）
    if (m_fourCard.size() == 1 && m_twoCard.size() == 1 &&
        m_oneCard.isEmpty() && m_threeCard.isEmpty())
    {
        m_type = Hand_Bomb_Two_Single;
        m_pt = m_fourCard.first();
        m_extra = 2;
        return true;
    }
    return false;
}
bool PlayHand::isBombJokers() { /* TODO(candidate) */
    if (m_oneCard.size() == 2 && containsBothJokers(m_oneCard) && m_twoCard.isEmpty() && m_threeCard.isEmpty() && m_fourCard.isEmpty())
    {
        m_type = Hand_Bomb_Jokers;
        m_pt = Card::Card_BJ;
        m_extra = 1;
        return true;
    }
    return false;
}
bool PlayHand::isBombJokersSingle() { /* TODO(candidate) */
    if (m_oneCard.size() == 3 && containsBothJokers(m_oneCard) && m_twoCard.isEmpty() && m_threeCard.isEmpty() && m_fourCard.isEmpty())
    {
        m_type = Hand_Bomb_Jokers_Single;
        m_pt = Card::Card_BJ;
        m_extra = 1;
        return true;
    }
    return false;
}
bool PlayHand::isBombJokersPair() { /* TODO(candidate) */
    if (m_oneCard.size() == 2 && containsBothJokers(m_oneCard) &&
        m_twoCard.size() == 1 && m_threeCard.isEmpty() && m_fourCard.isEmpty())
    {
        m_type = Hand_Bomb_Jokers_Pair;
        m_pt = Card::Card_BJ;
        m_extra = 1;
        return true;
    }
    return false;
}
bool PlayHand::isBombJokersTwoSingle() { /* TODO(candidate) */
    if (m_oneCard.size() == 4 && containsBothJokers(m_oneCard) &&
        m_twoCard.isEmpty() && m_threeCard.isEmpty() && m_fourCard.isEmpty())
    {
        m_type = Hand_Bomb_Jokers_Two_Single;
        m_pt = Card::Card_BJ;
        m_extra = 2;
        return true;
    }
    return false;
}

bool PlayHand::canBeat(const PlayHand &other) const
{
    // TODO(candidate): 按“火箭 > 炸弹 > 其它；同类型同长度比主点数”的规则比较。

    if (m_type == Hand_Unknown || other.m_type == Hand_Unknown)
        return false;

    // 处理Hand_Pass
    if (other.m_type == Hand_Pass)
        return m_type != Hand_Pass;
    if (m_type == Hand_Pass)
        return false;

    // 纯王炸（火箭）
    if (other.m_type == Hand_Bomb_Jokers)
        return false;
    if (m_type == Hand_Bomb_Jokers)
        return true;

    // 纯炸弹
    if (other.m_type == Hand_Bomb)
    {
        // 只有纯炸弹且点数更大才能压
        if (m_type != Hand_Bomb)
            return false;
        return  m_pt > other.m_pt;
    }
    if (m_type == Hand_Bomb)
    {
        // 纯炸弹压任何非纯王炸、非纯炸弹的牌型
        return true;
    }

    //到这里已无特殊炸牌，现在的非炸牌型必须类型完全相同才能压
    if (m_type != other.m_type)
        return false;

    // 连续牌型需长度一致
    switch (m_type) {
        case Hand_Seq_Single:
        case Hand_Seq_Pair:
        case Hand_Plane:
        case Hand_Plane_Two_Single:
        case Hand_Plane_Two_Pair:
            if (m_extra != other.m_extra)
                return false;
            break;
        default:
            break;
    }

    //剩下单、对、三张的牌型比较主点数
    return m_pt > other.m_pt;
}

QString PlayHand::handTypeName(HandType type)
{
    // TODO(candidate): 返回各牌型对应的中文名称。
    switch (type) {
    case Hand_Pass:
        return QStringLiteral("不要");
    case Hand_Single:
        return QStringLiteral("单张");
    case Hand_Pair:
        return QStringLiteral("对子");
    case Hand_Triple:
        return QStringLiteral("三张");
    case Hand_Triple_Single:
        return QStringLiteral("三带一");
    case Hand_Triple_Pair:
        return QStringLiteral("三带二");
    case Hand_Plane:
        return QStringLiteral("飞机");
    case Hand_Plane_Two_Single:
        return QStringLiteral("飞机带单");
    case Hand_Plane_Two_Pair:
        return QStringLiteral("飞机带双");
    case Hand_Seq_Pair:
        return QStringLiteral("连对");
    case Hand_Seq_Single:
        return QStringLiteral("顺子");
    case Hand_Bomb:
        return QStringLiteral("炸弹");
    case Hand_Bomb_Single:
        return QStringLiteral("四带二");        // 炸弹 + 两张单牌（6张）
    case Hand_Bomb_Pair:
        return QStringLiteral("四带两对");      // 炸弹 + 两对（8张）
    case Hand_Bomb_Two_Single:
        return QStringLiteral("四带一对");      // 炸弹 + 一对（6张）
    case Hand_Bomb_Jokers:
        return QStringLiteral("王炸");
    case Hand_Bomb_Jokers_Single:
        return QStringLiteral("双王带单");
    case Hand_Bomb_Jokers_Pair:
        return QStringLiteral("双王带对");
    case Hand_Bomb_Jokers_Two_Single:
        return QStringLiteral("双王带两单");
    default:
        return QStringLiteral("未知");
    }
}
