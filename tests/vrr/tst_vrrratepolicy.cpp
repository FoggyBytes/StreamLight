#include <QtTest>
#include <limits>

#include "../../app/streaming/vrrratepolicy.h"

class VrrRatePolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void calculatedRates();
    void protectedRates();
    void adaptiveHeadroomQualification();
    void vrrChoicesRestoreHeadroom();
    void disabledChoicesKeepNativeRefresh();
    void savedChoicesRemainSelectable();
};

void VrrRatePolicyTest::calculatedRates()
{
    QCOMPARE(VrrRatePolicy::vrrRateForRefresh(60), 59);
    QCOMPARE(VrrRatePolicy::vrrRateForRefresh(120), 116);
    QCOMPARE(VrrRatePolicy::vrrRateForRefresh(144), 138);
    QCOMPARE(VrrRatePolicy::vrrRateForRefresh(165), 157);
    for (int invalid : {-1, 0, 1, 3600, std::numeric_limits<int>::max()}) {
        QCOMPARE(VrrRatePolicy::vrrRateForRefresh(invalid), 0);
    }
    QCOMPARE(VrrRatePolicy::lowLatencyRateForRefresh(60), 50);
    QCOMPARE(VrrRatePolicy::lowLatencyRateForRefresh(120), 100);
    QCOMPARE(VrrRatePolicy::lowLatencyRateForRefresh(144), 120);
    QCOMPARE(VrrRatePolicy::lowLatencyRateForRefresh(165), 135);
    QCOMPARE(VrrRatePolicy::lowLatencyRateForRefresh(0), 0);

}

void VrrRatePolicyTest::protectedRates()
{
    QCOMPARE(VrrRatePolicy::protectedRateForRefresh(60), 59);
    QCOMPARE(VrrRatePolicy::protectedRateForRefresh(120), 116);
    QCOMPARE(VrrRatePolicy::protectedRateForRefresh(144), 138);
    QCOMPARE(VrrRatePolicy::protectedRateForRefresh(165), 157);
    QCOMPARE(VrrRatePolicy::protectedRateForRefresh(240), 224);
    QCOMPARE(VrrRatePolicy::protectedRateForRefresh(360), 324);
    for (int invalid : {-1, 0, 1, 3600, std::numeric_limits<int>::max()}) {
        QCOMPARE(VrrRatePolicy::protectedRateForRefresh(invalid), 0);
    }
}

void VrrRatePolicyTest::adaptiveHeadroomQualification()
{
    QVERIFY(VrrRatePolicy::hasAdaptiveHeadroom(59, 60));
    QVERIFY(VrrRatePolicy::hasAdaptiveHeadroom(116, 120));
    QVERIFY(VrrRatePolicy::hasAdaptiveHeadroom(138, 144));

    QVERIFY(VrrRatePolicy::hasAdaptiveHeadroom(60, 60));
    QVERIFY(VrrRatePolicy::hasAdaptiveHeadroom(119, 120));
    QVERIFY(VrrRatePolicy::hasAdaptiveHeadroom(120, 120));
    QVERIFY(!VrrRatePolicy::hasAdaptiveHeadroom(121, 120));
    QVERIFY(!VrrRatePolicy::hasAdaptiveHeadroom(0, 120));
    QVERIFY(!VrrRatePolicy::hasAdaptiveHeadroom(60, 0));
}

void VrrRatePolicyTest::vrrChoicesRestoreHeadroom()
{
    const std::vector<VrrFpsChoice> choices = VrrRatePolicy::buildChoices({120}, 90, true);

    QCOMPARE(static_cast<int>(choices.size()), 6);
    QCOMPARE(choices[0].fps, 30);
    QCOMPARE(choices[0].kind, VrrFpsChoiceKind::Fixed);
    QCOMPARE(choices[1].fps, 60);
    QCOMPARE(choices[1].kind, VrrFpsChoiceKind::Fixed);
    QCOMPARE(choices[2].fps, 90);
    QCOMPARE(choices[2].kind, VrrFpsChoiceKind::Custom);
    QCOMPARE(choices[3].fps, 100);
    QCOMPARE(choices[3].kind, VrrFpsChoiceKind::LowLatencyVrr);
    QCOMPARE(choices[4].fps, 116);
    QCOMPARE(choices[4].kind, VrrFpsChoiceKind::Vrr);
    QCOMPARE(choices[5].fps, 120);
    QCOMPARE(choices[5].kind, VrrFpsChoiceKind::Fixed);

    // Multiple displays and duplicate modes must not duplicate choices.
    const auto multiple = VrrRatePolicy::buildChoices({0, 1, 60, 144, 120, 120}, 116, true);
    const std::vector<int> expected = {30, 50, 59, 60, 100, 116, 120, 138, 144};
    QCOMPARE(multiple.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        QCOMPARE(multiple[i].fps, expected[i]);
    }
    QCOMPARE(multiple[3].kind, VrrFpsChoiceKind::Fixed);
    QCOMPARE(multiple[6].kind, VrrFpsChoiceKind::Fixed);
    QCOMPARE(multiple[8].kind, VrrFpsChoiceKind::Fixed);

}

void VrrRatePolicyTest::disabledChoicesKeepNativeRefresh()
{
    const std::vector<VrrFpsChoice> choices = VrrRatePolicy::buildChoices({120, 144}, 90, false);

    QCOMPARE(static_cast<int>(choices.size()), 5);
    QCOMPARE(choices[0].fps, 30);
    QCOMPARE(choices[1].fps, 60);
    QCOMPARE(choices[2].fps, 90);
    QCOMPARE(static_cast<int>(choices[2].kind), static_cast<int>(VrrFpsChoiceKind::Custom));
    QCOMPARE(choices[3].fps, 120);
    QCOMPARE(static_cast<int>(choices[3].kind), static_cast<int>(VrrFpsChoiceKind::Fixed));
    QCOMPARE(choices[4].fps, 144);
    QCOMPARE(static_cast<int>(choices[4].kind), static_cast<int>(VrrFpsChoiceKind::Fixed));
}

void VrrRatePolicyTest::savedChoicesRemainSelectable()
{
    for (int saved : {90, 100, 116, 120}) {
        const auto choices = VrrRatePolicy::buildChoices({120}, saved, true);
        int matches = 0;
        for (const auto& choice : choices) {
            if (choice.fps == saved) {
                ++matches;
                QCOMPARE(choice.kind, saved == 100 ? VrrFpsChoiceKind::LowLatencyVrr :
                         saved == 116 ? VrrFpsChoiceKind::Vrr :
                         saved == 120 ? VrrFpsChoiceKind::Fixed : VrrFpsChoiceKind::Custom);
            }
        }
        QCOMPARE(matches, 1);
    }

}

QTEST_APPLESS_MAIN(VrrRatePolicyTest)

#include "tst_vrrratepolicy.moc"
