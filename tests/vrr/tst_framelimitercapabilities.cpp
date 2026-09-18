#include <QtTest>
#include "../../app/backend/framelimitercapabilities.h"

class FrameLimiterCapabilitiesTest : public QObject
{
    Q_OBJECT
private slots:
    void oldHostAndDowngrade()
    {
        auto caps = FrameLimiterCapabilities::fromServerInfo(
            "<root><FrameLimiterSupported>1</FrameLimiterSupported><FrameLimiterEnabled>1</FrameLimiterEnabled></root>");
        QVERIFY(caps.supported && caps.enabled);
        caps = FrameLimiterCapabilities::fromServerInfo("<root><hostname>Old host</hostname></root>");
        QVERIFY(!caps.supported && !caps.enabled && !caps.virtualDisplayEnabled);
        QCOMPARE(caps.fpsLimitMilliHz, uint32_t(0));
    }
    void virtualLimitingWithoutManualSwitch()
    {
        const auto caps = FrameLimiterCapabilities::fromServerInfo(
            "<root><FrameLimiterSupported>1</FrameLimiterSupported><FrameLimiterEnabled>1</FrameLimiterEnabled>"
            "<VirtualDisplayFrameLimiterEnabled>1</VirtualDisplayFrameLimiterEnabled></root>");
        QVERIFY(caps.supported && caps.enabled && caps.virtualDisplayEnabled);
    }
    void integratedButDisabled()
    {
        const auto caps = FrameLimiterCapabilities::fromServerInfo(
            "<root><FrameLimiterSupported>true</FrameLimiterSupported><FrameLimiterEnabled>false</FrameLimiterEnabled>"
            "<VirtualDisplayFrameLimiterEnabled>false</VirtualDisplayFrameLimiterEnabled></root>");
        QVERIFY(caps.supported && !caps.enabled && !caps.virtualDisplayEnabled);
    }
    void overrideAndBooleanCompatibility()
    {
        const auto caps = FrameLimiterCapabilities::fromServerInfo(
            "<root><FrameLimiterSupported> TRUE </FrameLimiterSupported><FrameLimiterEnabled>true</FrameLimiterEnabled>"
            "<FrameLimiterFpsLimitMilliHz>116000</FrameLimiterFpsLimitMilliHz></root>");
        QVERIFY(caps.supported && caps.enabled);
        QCOMPARE(caps.fpsLimitMilliHz, uint32_t(116000));
    }
    void malformedNeverClaimsSupport()
    {
        for (const QString xml : {QString("<root><FrameLimiterSupported>1</FrameLimiterSupported>"),
                                  QString("<root><FrameLimiterSupported>yes</FrameLimiterSupported></root>")}) {
            QVERIFY(!FrameLimiterCapabilities::fromServerInfo(xml).supported);
        }
    }
};
QTEST_APPLESS_MAIN(FrameLimiterCapabilitiesTest)
#include "tst_framelimitercapabilities.moc"
