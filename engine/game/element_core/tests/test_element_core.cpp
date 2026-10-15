// =============================================================================
// CHROMODYNAMIC — cd::element_core unit tests (FAZ 5 Wave 5.1)
// =============================================================================
#include <cd/element_core/Element.hpp>

#include <gtest/gtest.h>

namespace
{

class TickCounterElement final : public cd::element_core::Spatial3D
{
public:
    explicit TickCounterElement(std::string n)
        : Spatial3D(std::move(n))
    {
    }

    [[nodiscard]] std::string_view class_name() const noexcept override
    {
        return "TickCounterElement";
    }

    void on_mount() override { ++mount_count; }
    void on_unmount() override { ++unmount_count; }
    void on_tick(float dt) override { total_dt += dt; }
    void on_fixed_tick(float fixed_dt) override { total_fixed_dt += fixed_dt; }

    int mount_count { 0 };
    int unmount_count { 0 };
    float total_dt { 0.0F };
    float total_fixed_dt { 0.0F };
};

TEST(ElementCoreWave51, PulseSignalConnectEmitDisconnect)
{
    cd::element_core::Pulse<int, float> pulse;
    int sum_i = 0;
    float sum_f = 0.0F;

    const auto id = pulse.connect([&](int i, float f) {
        sum_i += i;
        sum_f += f;
    });
    EXPECT_EQ(pulse.subscriber_count(), 1U);

    pulse.emit(4, 1.5F);
    EXPECT_EQ(sum_i, 4);
    EXPECT_FLOAT_EQ(sum_f, 1.5F);

    EXPECT_TRUE(pulse.disconnect(id));
    pulse.emit(10, 2.0F);
    EXPECT_EQ(sum_i, 4);
}

TEST(ElementCoreWave51, HierarchySpatialTransformLifecycleAndStageSerialize)
{
    cd::element_core::Stage stage("CitadelCourtyard");
    auto& parent = stage.root().emplace_child<cd::element_core::Spatial3D>("ParentAnchor");
    parent.set_local_position(10.0F, 5.0F, -2.0F);
    parent.set_local_scale(2.0F, 2.0F, 2.0F);

    auto& child = parent.emplace_child<TickCounterElement>("HeroBeacon");
    child.set_local_position(1.0F, 2.0F, 3.0F);

    stage.mount();
    EXPECT_EQ(child.mount_count, 1);

    // World position = parent_pos + child_pos * parent_scale = (10+2, 5+4, -2+6) = (12, 9, 4)
    const auto wp = child.world_position();
    EXPECT_FLOAT_EQ(wp[0], 12.0F);
    EXPECT_FLOAT_EQ(wp[1], 9.0F);
    EXPECT_FLOAT_EQ(wp[2], 4.0F);

    stage.tick(0.016F);
    stage.fixed_tick(0.020F);
    EXPECT_NEAR(child.total_dt, 0.016F, 1e-5F);
    EXPECT_NEAR(child.total_fixed_dt, 0.020F, 1e-5F);

    EXPECT_EQ(stage.find_by_name("HeroBeacon"), &child);
    const auto found_counters = stage.find_all<TickCounterElement>();
    ASSERT_EQ(found_counters.size(), 1U);
    EXPECT_EQ(found_counters[0], &child);

    const std::string text = stage.serialize_cdstage();
    EXPECT_NE(text.find("stage \"CitadelCourtyard\""), std::string::npos);
    EXPECT_NE(text.find("HeroBeacon"), std::string::npos);

    stage.unmount();
    EXPECT_EQ(child.unmount_count, 1);
}

}  // namespace
