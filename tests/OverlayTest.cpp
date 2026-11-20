#include "DeviceTreeDiff.h"
#include "DeviceTreeParser.h"
#include "Overlay.h"
#include "Process.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <cstdlib>

using namespace DTE;
using DTE::test::findProp;
using DTE::test::TempDir;

namespace {

const std::string kOverlayDir = std::string(TEST_DATA_DIR) + "/overlay";

std::shared_ptr<DeviceTreeNode> node(const std::string& name) {
    return std::make_shared<DeviceTreeNode>(name);
}

std::string diffReport(const std::shared_ptr<DeviceTree>& a, const std::shared_ptr<DeviceTree>& b) {
    DeviceTreeDiff diff(a, b);
    return diff.getTotalChanges() == 0 ? std::string() : DiffVisualizer(diff).getFormattedDiff();
}

} // namespace

// ---------------------------------------------------------------------------
// Overlays built by hand (no dtc needed)
// ---------------------------------------------------------------------------

TEST(Overlay, Detection) {
    DeviceTree plain;
    plain.getRoot()->addChild(node("soc"));
    EXPECT_FALSE(isOverlay(plain));

    DeviceTree overlay;
    auto fragment = node("fragment@0");
    fragment->addChild(node("__overlay__"));
    overlay.getRoot()->addChild(fragment);
    EXPECT_TRUE(isOverlay(overlay));
}

TEST(Overlay, RejectsNonOverlayAndLeavesBaseUntouched) {
    DeviceTree base;
    auto before = base.getRoot();
    DeviceTree notOverlay;
    std::string error;
    EXPECT_FALSE(applyOverlay(base, notOverlay, &error));
    EXPECT_NE(error.find("not an overlay"), std::string::npos) << error;
    EXPECT_EQ(base.getRoot(), before);
}

TEST(Overlay, TargetPathMergeAndFailureIsAtomic) {
    DeviceTree base;
    auto soc = node("soc");
    soc->addProperty(DeviceTreeProperty("status", std::string("disabled")));
    base.getRoot()->addChild(soc);

    // Two fragments: the first is fine, the second targets a missing path.
    DeviceTree overlay;
    auto f0 = node("fragment@0");
    f0->addProperty(DeviceTreeProperty("target-path", std::string("/soc")));
    auto o0 = node("__overlay__");
    o0->addProperty(DeviceTreeProperty("status", std::string("okay")));
    o0->addChild(node("child"));
    f0->addChild(o0);
    auto f1 = node("fragment@1");
    f1->addProperty(DeviceTreeProperty("target-path", std::string("/missing")));
    f1->addChild(node("__overlay__"));
    overlay.getRoot()->addChild(f0);
    overlay.getRoot()->addChild(f1);

    auto before = base.getRoot();
    std::string error;
    EXPECT_FALSE(applyOverlay(base, overlay, &error));
    EXPECT_NE(error.find("target-path /missing is not in the base"), std::string::npos) << error;
    EXPECT_EQ(base.getRoot(), before);
    EXPECT_EQ(std::get<std::string>(soc->findProperty("status")->getValue()), "disabled");

    overlay.getRoot()->removeChild(f1);
    ASSERT_TRUE(applyOverlay(base, overlay, &error)) << error;
    EXPECT_EQ(std::get<std::string>(base.findNodeByPath("/soc")->findProperty("status")->getValue()), "okay");
    EXPECT_TRUE(base.findNodeByPath("/soc/child"));
}

TEST(Overlay, MissingSymbolsExplainsHowToFix) {
    DeviceTree base;
    DeviceTree overlay;
    auto f0 = node("fragment@0");
    f0->addProperty(DeviceTreeProperty("target", std::vector<uint32_t>{0xffffffff}));
    f0->addChild(node("__overlay__"));
    auto fixups = node("__fixups__");
    fixups->addProperty(DeviceTreeProperty("uart0", std::string("/fragment@0:target:0")));
    overlay.getRoot()->addChild(f0);
    overlay.getRoot()->addChild(fixups);

    std::string error;
    EXPECT_FALSE(applyOverlay(base, overlay, &error));
    EXPECT_NE(error.find("label 'uart0'"), std::string::npos) << error;
    EXPECT_NE(error.find("dtc -@"), std::string::npos) << error;
}

// ---------------------------------------------------------------------------
// Real overlays compiled by dtc
// ---------------------------------------------------------------------------

#ifdef DTC_PATH
class OverlayWithDtc : public ::testing::Test {
protected:
    void SetUp() override {
        ::setenv("DTE_DTC", DTC_PATH, 1);
        LoadOptions symbols;
        symbols.symbols = true;
        ASSERT_TRUE(overlay_.loadFromFile(kOverlayDir + "/overlay.dtso", symbols)) << overlay_.getLoadError();
    }
    void TearDown() override { ::unsetenv("DTE_DTC"); }

    DeviceTree overlay_;
    TempDir tmp_;
};

TEST_F(OverlayWithDtc, CompiledOverlayIsDetected) {
    EXPECT_TRUE(isOverlay(overlay_));
    DeviceTree base;
    ASSERT_TRUE(base.loadFromFile(kOverlayDir + "/base.dts"));
    EXPECT_FALSE(isOverlay(base));
}

#ifdef FDTOVERLAY_PATH
// The reference: libfdt's fdtoverlay on the same inputs.
TEST_F(OverlayWithDtc, MatchesFdtoverlay) {
    const auto baseDtb = (tmp_.path() / "base.dtb").string();
    const auto overlayDtbo = (tmp_.path() / "overlay.dtbo").string();
    const auto mergedDtb = (tmp_.path() / "merged.dtb").string();
    auto r = runProcess({DTC_PATH, "-q", "-@", "-I", "dts", "-O", "dtb", "-o", baseDtb, kOverlayDir + "/base.dts"});
    ASSERT_TRUE(r.ok()) << r.stderrData;
    r = runProcess({DTC_PATH, "-q", "-@", "-I", "dts", "-O", "dtb", "-o", overlayDtbo, kOverlayDir + "/overlay.dtso"});
    ASSERT_TRUE(r.ok()) << r.stderrData;
    r = runProcess({FDTOVERLAY_PATH, "-i", baseDtb, "-o", mergedDtb, overlayDtbo});
    ASSERT_TRUE(r.ok()) << r.stderrData;

    auto ours = std::make_shared<DeviceTree>();
    ASSERT_TRUE(ours->loadFromFile(baseDtb));
    DeviceTree overlayBlob;
    ASSERT_TRUE(overlayBlob.loadFromFile(overlayDtbo));
    std::string error;
    ASSERT_TRUE(applyOverlay(*ours, overlayBlob, &error)) << error;

    auto reference = std::make_shared<DeviceTree>();
    ASSERT_TRUE(reference->loadFromFile(mergedDtb));
    EXPECT_EQ(diffReport(ours, reference), "");
}
#endif

TEST_F(OverlayWithDtc, AppliesToDtsBaseWithoutSymbols) {
    // A DTS base loaded normally has no __symbols__; labels come from a
    // separate -@ compile.
    DeviceTree base;
    ASSERT_TRUE(base.loadFromFile(kOverlayDir + "/base.dts"));
    DeviceTree withSymbols;
    LoadOptions symbols;
    symbols.symbols = true;
    ASSERT_TRUE(withSymbols.loadFromFile(kOverlayDir + "/base.dts", symbols));
    const SymbolMap labels = symbolsOf(withSymbols);
    ASSERT_EQ(labels.at("uart0"), "/soc/serial@3000");

    // uart0 and clk have no phandle yet (nothing in the base refers to them)
    EXPECT_FALSE(findProp(base, "/soc/serial@3000", "phandle"));

    std::string error;
    ASSERT_TRUE(applyOverlay(base, overlay_, &error, &labels)) << error;

    EXPECT_FALSE(base.findNodeByPath("/__symbols__")); // not added
    EXPECT_EQ(std::get<std::string>(findProp(base, "/soc/serial@3000", "status")->getValue()), "okay");
    EXPECT_EQ(std::get<std::string>(findProp(base, "/", "model")->getValue()), "Overlay base + sensor");
    EXPECT_TRUE(findProp(base, "/", "overlaid")->isEmpty());
    ASSERT_TRUE(base.findNodeByPath("/soc/sensor@4000"));
    ASSERT_TRUE(base.findNodeByPath("/soc/thermal"));

    // Every reference resolves to the right node
    std::vector<std::string> refs;
    for (const auto& ref : base.references()) {
        ASSERT_TRUE(ref.problem.empty()) << ref.property << ": " << ref.problem;
        refs.push_back(ref.source->getFullPath() + ":" + ref.property + " -> " + ref.target->getFullPath());
    }
    EXPECT_EQ(refs, (std::vector<std::string>{
                        "/:interrupt-parent -> /interrupt-controller@1000",
                        "/soc/serial@3000:clocks -> /soc/clock-controller@2000",
                        "/soc/sensor@4000:interrupts-extended -> /interrupt-controller@1000",
                        "/soc/thermal:thermal-sensors -> /soc/sensor@4000",
                    }));
    // The overlay's self reference was renumbered consistently
    auto sensor = base.findNodeByPath("/soc/sensor@4000");
    EXPECT_EQ(sensor->findProperty("self")->getValueAsCells(), sensor->findProperty("phandle")->getValueAsCells());

    // Phandles are unique and the result validates
    std::vector<std::string> issues;
    for (const auto& i : base.check()) issues.push_back(i.toString());
    EXPECT_EQ(issues, std::vector<std::string>{});
}

TEST_F(OverlayWithDtc, UpdatesBaseSymbols) {
    DeviceTree base;
    LoadOptions symbols;
    symbols.symbols = true;
    ASSERT_TRUE(base.loadFromFile(kOverlayDir + "/base.dts", symbols));
    std::string error;
    ASSERT_TRUE(applyOverlay(base, overlay_, &error)) << error;
    const auto labels = symbolsOf(base);
    EXPECT_EQ(labels.at("sensor"), "/soc/sensor@4000");
    EXPECT_EQ(labels.at("uart0"), "/soc/serial@3000");
}

TEST_F(OverlayWithDtc, BaseWithoutSymbolsAndNoMapFails) {
    DeviceTree base;
    ASSERT_TRUE(base.loadFromFile(kOverlayDir + "/base.dts"));
    auto before = base.getRoot();
    std::string error;
    EXPECT_FALSE(applyOverlay(base, overlay_, &error));
    EXPECT_NE(error.find("dtc -@"), std::string::npos) << error;
    EXPECT_EQ(base.getRoot(), before);
}
#endif
