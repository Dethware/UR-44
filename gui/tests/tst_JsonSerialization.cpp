#include <QtTest>

#include "JsonSerialization.h"

namespace {
struct Inner
{
    int32_t value = 7;
    QJsonObject extra;

    template<typename S> void serialize(S& fp)
    {
        fp.property("value", value);
    }
};

struct Outer
{
    uint32_t u = 1;
    int32_t i = 2;
    bool b = true;
    QString s = "default";
    QList<int32_t> list;
    std::array<uint32_t, 3> fixed{9, 9, 9};
    std::pair<QString, int32_t> pair{"a", 1};
    Inner inner;
    QList<Inner> inners;
    QJsonObject extra;

    template<typename S> void serialize(S& fp)
    {
        fp.property("u", u);
        fp.property("i", i);
        fp.property("b", b);
        fp.property("s", s);
        fp.property("list", list);
        fp.property("fixed", fixed);
        fp.property("pair", pair);
        fp.property("inner", inner);
        fp.property("inners", inners);
    }
};

Outer read(const QJsonObject& json)
{
    Outer outer;
    JsonReader::value(QJsonValue(json), outer);
    return outer;
}
}

class JsonSerializationTest : public QObject
{
    Q_OBJECT

private slots:
    void read_allTypes()
    {
        Outer o = read(QJsonObject{
            {"u", 4000000000.0},
            {"i", -3},
            {"b", false},
            {"s", "text"},
            {"list", QJsonArray{1, 2, 3}},
            {"fixed", QJsonArray{4, 5, 6}},
            {"pair", QJsonObject{{"first", "key"}, {"second", 5}}},
            {"inner", QJsonObject{{"value", 8}}},
            {"inners", QJsonArray{QJsonObject{{"value", 1}}, QJsonObject{}}},
        });
        QCOMPARE(o.u, uint32_t(4000000000u));
        QCOMPARE(o.i, -3);
        QCOMPARE(o.b, false);
        QCOMPARE(o.s, QString("text"));
        QCOMPARE(o.list, (QList<int32_t>{1, 2, 3}));
        QCOMPARE(o.fixed, (std::array<uint32_t, 3>{4, 5, 6}));
        QCOMPARE(o.pair.first, QString("key"));
        QCOMPARE(o.pair.second, 5);
        QCOMPARE(o.inner.value, 8);
        QCOMPARE(o.inners.size(), 2);
        QCOMPARE(o.inners[0].value, 1);
        QCOMPARE(o.inners[1].value, 7);
    }

    void read_missingKeepsDefaults()
    {
        Outer o = read(QJsonObject{});
        Outer d;
        QCOMPARE(o.u, d.u);
        QCOMPARE(o.i, d.i);
        QCOMPARE(o.b, d.b);
        QCOMPARE(o.s, d.s);
        QCOMPARE(o.fixed, d.fixed);
        QCOMPARE(o.pair, d.pair);
        QCOMPARE(o.inner.value, d.inner.value);
    }

    void read_wrongTypesKeepDefaults()
    {
        Outer o = read(QJsonObject{
            {"u", -1},
            {"i", 1.5},
            {"b", 1},
            {"s", 5},
            {"list", "nope"},
            {"fixed", QJsonArray{1, 2}},
            {"pair", QJsonArray{1, 2}},
            {"inner", 3},
        });
        Outer d;
        QCOMPARE(o.u, d.u);
        QCOMPARE(o.i, d.i);
        QCOMPARE(o.b, d.b);
        QCOMPARE(o.s, d.s);
        QVERIFY(o.list.isEmpty());
        QCOMPARE(o.fixed, d.fixed);
        QCOMPARE(o.pair, d.pair);
        QCOMPARE(o.inner.value, d.inner.value);
    }

    void read_outOfRangeIntegers()
    {
        Outer o = read(QJsonObject{{"u", 5000000000.0}, {"i", 3000000000.0}});
        QCOMPARE(o.u, uint32_t(1));
        QCOMPARE(o.i, 2);
    }

    void write_roundTrip()
    {
        QJsonObject json{
            {"u", 4000000000.0},
            {"i", -3},
            {"b", false},
            {"s", "text"},
            {"list", QJsonArray{1, 2, 3}},
            {"fixed", QJsonArray{4, 5, 6}},
            {"pair", QJsonObject{{"first", "key"}, {"second", 5}}},
            {"inner", QJsonObject{{"value", 8}}},
            {"inners", QJsonArray{QJsonObject{{"value", 1}}}},
        };
        Outer o = read(json);
        QCOMPARE(JsonWriter::value(o).toObject(), json);
    }

    void write_keepsUnknownKeys()
    {
        QJsonObject json{{"unknown", "top"}, {"inner", QJsonObject{{"value", 1}, {"unknown", true}}}};
        Outer o = read(json);
        QCOMPARE(o.extra, (QJsonObject{{"unknown", "top"}}));
        QCOMPARE(o.inner.extra, (QJsonObject{{"unknown", true}}));

        QJsonObject written = JsonWriter::value(o).toObject();
        QCOMPARE(written["unknown"].toString(), QString("top"));
        QCOMPARE(written["inner"].toObject()["unknown"].toBool(), true);
    }
};

QTEST_GUILESS_MAIN(JsonSerializationTest)
#include "tst_JsonSerialization.moc"
