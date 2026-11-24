#include "DeviceTreeDiff.h"
#include "DeviceTreeParser.h"
#include "Overlay.h"
#include "Process.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <functional>

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
    SymbolMap labels = symbolsOf(withSymbols);
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

#ifdef DTC_PATH
TEST_F(OverlayWithDtc, LoadHelpersAndChainedOverlays) {
    DeviceTree base;
    ASSERT_TRUE(base.loadFromFile(kOverlayDir + "/base.dts"));
    SymbolMap labels = baseSymbolsFor(base, kOverlayDir + "/base.dts");
    EXPECT_EQ(labels.at("clk"), "/soc/clock-controller@2000");

    DeviceTree overlay;
    std::string error;
    ASSERT_TRUE(loadOverlay(overlay, kOverlayDir + "/overlay.dtso", {}, &error)) << error;
    ASSERT_TRUE(applyOverlay(base, overlay, &error, &labels)) << error;
    // Labels added by the first overlay are available to the next one
    EXPECT_EQ(labels.at("sensor"), "/soc/sensor@4000");

    const auto second = tmp_.write("second.dtso", "/dts-v1/;\n/plugin/;\n&sensor { status = \"disabled\"; };\n");
    DeviceTree overlay2;
    ASSERT_TRUE(loadOverlay(overlay2, second, {}, &error)) << error;
    ASSERT_TRUE(applyOverlay(base, overlay2, &error, &labels)) << error;
    EXPECT_EQ(std::get<std::string>(findProp(base, "/soc/sensor@4000", "status")->getValue()), "disabled");

    // A plain tree is rejected by loadOverlay
    DeviceTree notOverlay;
    EXPECT_FALSE(loadOverlay(notOverlay, kOverlayDir + "/base.dts", {}, &error));
    EXPECT_NE(error.find("is not an overlay"), std::string::npos) << error;
}
#endif

// ---------------------------------------------------------------------------
// Generating overlays from a diff
// ---------------------------------------------------------------------------

namespace {

// Renumbers phandles by node path and rewrites decoded references, so trees
// that differ only in phandle numbering (as after applying an overlay)
// compare equal, while wrong references still show up.
std::shared_ptr<DeviceTree> canonical(const DeviceTree& t) {
    auto copy = std::make_shared<DeviceTree>(t);
    copy->setRoot(t.getRoot()->clone());
    const auto refs = copy->references();

    std::map<std::string, std::shared_ptr<DeviceTreeNode>> withPhandle; // sorted by path
    std::function<void(const std::shared_ptr<DeviceTreeNode>&)> walk = [&](const std::shared_ptr<DeviceTreeNode>& n) {
        if (static_cast<const DeviceTreeNode&>(*n).findProperty("phandle")) withPhandle[n->getFullPath()] = n;
        for (const auto& c : n->getChildren()) walk(c);
    };
    walk(copy->getRoot());
    std::map<uint32_t, uint32_t> renumber;
    uint32_t next = 1;
    for (const auto& [path, n] : withPhandle) {
        renumber[n->findProperty("phandle")->getValueAsCells()[0]] = next;
        n->addProperty(DeviceTreeProperty("phandle", std::vector<uint32_t>{next++}));
    }
    std::map<std::pair<DeviceTreeNode*, std::string>, std::vector<uint32_t>> cells;
    for (const auto& r : refs) {
        if (!r.problem.empty()) continue;
        auto key = std::make_pair(r.source.get(), r.property);
        if (!cells.count(key)) cells[key] = r.source->findProperty(r.property)->getValueAsCells();
        cells[key][r.cellIndex] = renumber.at(r.phandle);
    }
    for (const auto& [key, v] : cells) key.first->addProperty(DeviceTreeProperty(key.second, v));
    return copy;
}

std::vector<std::string> diffKeys(const std::shared_ptr<DeviceTree>& a, const std::shared_ptr<DeviceTree>& b) {
    std::vector<std::string> out;
    for (const auto& e : DeviceTreeDiff(a, b).generateDiff()) {
        const char* t = e.type == DiffType::Added ? "+" : e.type == DiffType::Removed ? "-" : "~";
        out.push_back(t + e.path + (e.propertyName.empty() ? "" : ":" + e.propertyName));
    }
    return out;
}

} // namespace

TEST(GenerateOverlay, NothingToDoForIdenticalTrees) {
    DeviceTree a;
    a.getRoot()->addProperty(DeviceTreeProperty("model", std::string("x")));
    std::vector<std::string> warnings;
    EXPECT_EQ(generateOverlay(a, a, &warnings), "/dts-v1/;\n/plugin/;\n");
    EXPECT_TRUE(warnings.empty());
}

TEST(GenerateOverlay, RemovalsAreWarnings) {
    DeviceTree base;
    base.getRoot()->addProperty(DeviceTreeProperty("gone", std::string("x")));
    base.getRoot()->addChild(node("old"));
    DeviceTree target;
    std::vector<std::string> warnings;
    EXPECT_EQ(generateOverlay(base, target, &warnings), "/dts-v1/;\n/plugin/;\n");
    EXPECT_EQ(warnings, (std::vector<std::string>{"/:gone was removed; overlays cannot delete properties",
                                                  "/old was removed; overlays cannot delete nodes"}));
}

#ifdef DTC_PATH
class GenerateOverlayWithDtc : public ::testing::Test {
protected:
    void SetUp() override {
        ::setenv("DTE_DTC", DTC_PATH, 1);
        ASSERT_TRUE(base_.loadFromFile(std::string(TEST_DATA_DIR) + "/refs.dts")) << base_.getLoadError();

        // target = base with changes of every kind
        target_ = std::make_shared<DeviceTree>(base_);
        target_->setRoot(base_.getRoot()->clone());
        auto root = target_->getRoot();
        root->addProperty(DeviceTreeProperty("model", std::string("Changed \"model\"")));
        auto serial = target_->findNodeByPath("/serial@4000");
        serial->addProperty(DeviceTreeProperty("status", std::string("okay")));
        serial->removeProperty("clock-names");
        // a new node with a phandle, referenced from an existing node
        auto mbox = std::make_shared<DeviceTreeNode>("mbox@6000");
        mbox->addProperty(DeviceTreeProperty("reg", std::vector<uint32_t>{0x6000, 0x100}));
        mbox->addProperty(DeviceTreeProperty("#mbox-cells", std::vector<uint32_t>{1}));
        mbox->addProperty(DeviceTreeProperty("phandle", std::vector<uint32_t>{0x50}));
        root->addChild(mbox);
        serial->addProperty(DeviceTreeProperty("mboxes", std::vector<uint32_t>{0x50, 2}));
        // a new subtree referring to an existing node
        const uint32_t clk = target_->findNodeByPath("/clock-controller@2000")->findProperty("phandle")->getValueAsCells()[0];
        auto consumer = std::make_shared<DeviceTreeNode>("consumer");
        auto inner = std::make_shared<DeviceTreeNode>("inner");
        inner->addProperty(DeviceTreeProperty("clocks", std::vector<uint32_t>{clk, 7}));
        consumer->addChild(inner);
        root->addChild(consumer);

        source_ = generateOverlay(base_, *target_, &warnings_);
        file_ = tmp_.write("diff.dtso", source_);
    }
    void TearDown() override { ::unsetenv("DTE_DTC"); }

    DeviceTree base_;
    std::shared_ptr<DeviceTree> target_;
    std::string source_;
    std::vector<std::string> warnings_;
    std::string file_;
    TempDir tmp_;
};

TEST_F(GenerateOverlayWithDtc, WritesReadableSource) {
    EXPECT_NE(source_.find("&{/} {\n\tmodel = \"Changed \\\"model\\\"\";\n"), std::string::npos) << source_;
    EXPECT_NE(source_.find("&{/serial@4000} {\n"), std::string::npos) << source_;
    EXPECT_NE(source_.find("\tmboxes = <&n50 0x2>;\n"), std::string::npos) << source_;
    EXPECT_NE(source_.find("\tn50: mbox@6000 {\n"), std::string::npos) << source_;
    EXPECT_EQ(source_.find("phandle ="), std::string::npos) << source_; // dtc assigns them
    EXPECT_EQ(warnings_, (std::vector<std::string>{
                             "/serial@4000:clock-names was removed; overlays cannot delete properties"}));
}

TEST_F(GenerateOverlayWithDtc, ApplyingItReproducesTheTarget) {
    DeviceTree overlay;
    std::string error;
    ASSERT_TRUE(loadOverlay(overlay, file_, {}, &error)) << error << "\n" << source_;
    auto applied = std::make_shared<DeviceTree>(base_);
    applied->setRoot(base_.getRoot()->clone());
    ASSERT_TRUE(applyOverlay(*applied, overlay, &error)) << error;

    // Only the removal (which overlays cannot express) remains
    EXPECT_EQ(diffKeys(canonical(*applied), canonical(*target_)),
              (std::vector<std::string>{"-/serial@4000:clock-names"}));
    // And every reference still resolves to the right node
    auto mboxRef = applied->referencesIn(applied->findNodeByPath("/serial@4000"), "mboxes");
    ASSERT_EQ(mboxRef.size(), 1u);
    ASSERT_TRUE(mboxRef[0].target);
    EXPECT_EQ(mboxRef[0].target->getFullPath(), "/mbox@6000");
}

#ifdef FDTOVERLAY_PATH
TEST_F(GenerateOverlayWithDtc, FdtoverlayAgrees) {
    const auto baseDtb = (tmp_.path() / "base.dtb").string();
    const auto dtbo = (tmp_.path() / "diff.dtbo").string();
    const auto merged = (tmp_.path() / "merged.dtb").string();
    ASSERT_TRUE(base_.saveToFile(baseDtb));
    // No -@ needed: the overlay's own labels become __local_fixups__
    auto r = runProcess({DTC_PATH, "-q", "-I", "dts", "-O", "dtb", "-o", dtbo, file_});
    ASSERT_TRUE(r.ok()) << r.stderrData;
    r = runProcess({FDTOVERLAY_PATH, "-i", baseDtb, "-o", merged, dtbo});
    ASSERT_TRUE(r.ok()) << r.stderrData;
    DeviceTree reference;
    ASSERT_TRUE(reference.loadFromFile(merged));
    EXPECT_EQ(diffKeys(canonical(reference), canonical(*target_)),
              (std::vector<std::string>{"-/serial@4000:clock-names"}));
}
#endif

TEST_F(GenerateOverlayWithDtc, WarnsWhenAnExistingNodeNeedsAPhandle) {
    // In this base /oscillator has no phandle, while the target refers to
    // it (serial's clocks) and has one: an overlay cannot add a phandle to
    // an existing node, and the reference keeps a number the base lacks.
    auto base = std::make_shared<DeviceTree>(base_);
    base->setRoot(base_.getRoot()->clone());
    base->findNodeByPath("/oscillator")->removeProperty("phandle");
    base->findNodeByPath("/serial@4000")->removeProperty("clocks");
    std::vector<std::string> warnings;
    generateOverlay(*base, *target_, &warnings);
    auto has = [&](const std::string& w) { return std::find(warnings.begin(), warnings.end(), w) != warnings.end(); };
    EXPECT_TRUE(has("/oscillator: phandle changed or added; overlays cannot set it for existing nodes"));
    EXPECT_TRUE(has("/serial@4000:clocks refers to /oscillator, which has no phandle in the base; the reference is "
                    "written as a plain number"))
        << ::testing::PrintToString(warnings);
}
#endif
