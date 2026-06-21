// =============================================================================
// CHROMODYNAMIC — test_editor_charter_complete.cpp
// Charter-complete gap-close: PropertyModel, SelectionOutline,
// PropertyDrawer (DragSession), EditHistory coalescing.
//
// Pattern: Arrange / Act / Assert. No GPU, no ImGui.
// AAA + edge + negative for every surface.
// =============================================================================
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/PropertyDrawer.hpp>
#include <cd/editor/PropertyModel.hpp>
#include <cd/editor/SelectionOutline.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/math/Vector.hpp>
#include <gtest/gtest.h>

#include <string>
#include <vector>

// ===========================================================================
// Helpers
// ===========================================================================

namespace
{

/// Minimal ICommand that records an integer on apply/revert.
class ValueCmd final : public cd::editor::ICommand
{
public:
    explicit ValueCmd(int* target, int value_to_set, int value_to_revert)
        : target_ { target }
        , apply_val_ { value_to_set }
        , revert_val_ { value_to_revert }
    {}
    void apply()  override { *target_ = apply_val_; }
    void revert() override { *target_ = revert_val_; }
    [[nodiscard]] std::string_view label() const noexcept override { return "Value"; }
    [[nodiscard]] std::size_t byte_size() const noexcept override { return sizeof(*this); }

private:
    int* target_;
    int  apply_val_;
    int  revert_val_;
};

}  // namespace

// ===========================================================================
// PropertyModel — register, committed access, pending lifecycle
// ===========================================================================

TEST(PropertyModel, EmptyByDefault)
{
    const cd::editor::PropertyModel m;
    EXPECT_EQ(m.size(), 0U);
    EXPECT_FALSE(m.contains("any"));
}

TEST(PropertyModel, DefineRegistersAndGetReadsBack)
{
    cd::editor::PropertyModel m;
    m.define("roughness", 0.5F);
    ASSERT_TRUE(m.contains("roughness"));
    const auto v = m.get<float>("roughness");
    ASSERT_TRUE(v.has_value());
    EXPECT_FLOAT_EQ(*v, 0.5F);
}

TEST(PropertyModel, DefineSecondCallIgnoresNewInitial)
{
    cd::editor::PropertyModel m;
    m.define("x", 1.0F);
    m.define("x", 99.0F);  // must NOT overwrite
    EXPECT_FLOAT_EQ(m.get<float>("x").value_or(0.0F), 1.0F);
    EXPECT_EQ(m.size(), 1U);
}

TEST(PropertyModel, SetOverwritesCommittedValue)
{
    cd::editor::PropertyModel m;
    m.define("x", 1.0F);
    m.set("x", 7.0F);
    EXPECT_FLOAT_EQ(m.get<float>("x").value_or(0.0F), 7.0F);
}

TEST(PropertyModel, SetUnknownKeyIsNoOp)
{
    cd::editor::PropertyModel m;
    m.set("ghost", 3.14F);  // must not crash
    EXPECT_EQ(m.size(), 0U);
}

TEST(PropertyModel, GetTypeMismatchReturnsNullopt)
{
    cd::editor::PropertyModel m;
    m.define("flag", true);
    EXPECT_FALSE(m.get<float>("flag").has_value());
    EXPECT_FALSE(m.get<std::int64_t>("flag").has_value());
}

TEST(PropertyModel, GetUnknownKeyReturnsNullopt)
{
    const cd::editor::PropertyModel m;
    EXPECT_FALSE(m.get<float>("nope").has_value());
}

TEST(PropertyModel, BeginEditSetsPending)
{
    cd::editor::PropertyModel m;
    m.define("pos_x", 0.0F);
    m.begin_edit("pos_x", 3.5F);
    EXPECT_TRUE(m.has_pending("pos_x"));
    const auto pend = m.get_pending<float>("pos_x");
    ASSERT_TRUE(pend.has_value());
    EXPECT_FLOAT_EQ(*pend, 3.5F);
    // Committed value must remain unchanged.
    EXPECT_FLOAT_EQ(m.get<float>("pos_x").value_or(-1.0F), 0.0F);
}

TEST(PropertyModel, BeginEditUnknownKeyIsNoOp)
{
    cd::editor::PropertyModel m;
    m.begin_edit("ghost", 1.0F);  // must not crash
    EXPECT_FALSE(m.has_pending("ghost"));
}

TEST(PropertyModel, CommitFinalisesPendingAndReturnsDelta)
{
    cd::editor::PropertyModel m;
    m.define("exposure", 2.0F);
    m.begin_edit("exposure", 4.5F);
    const auto result = m.commit("exposure");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "exposure");
    // before == old committed
    const auto* before_f = std::get_if<float>(&result->before);
    ASSERT_NE(before_f, nullptr);
    EXPECT_FLOAT_EQ(*before_f, 2.0F);
    // after == new committed
    const auto* after_f = std::get_if<float>(&result->after);
    ASSERT_NE(after_f, nullptr);
    EXPECT_FLOAT_EQ(*after_f, 4.5F);
    // pending must be cleared
    EXPECT_FALSE(m.has_pending("exposure"));
    // committed must be updated
    EXPECT_FLOAT_EQ(m.get<float>("exposure").value_or(0.0F), 4.5F);
}

TEST(PropertyModel, CommitWithNoPendingReturnsNullopt)
{
    cd::editor::PropertyModel m;
    m.define("x", 1.0F);
    EXPECT_FALSE(m.commit("x").has_value());
}

TEST(PropertyModel, CommitUnknownKeyReturnsNullopt)
{
    cd::editor::PropertyModel m;
    EXPECT_FALSE(m.commit("ghost").has_value());
}

TEST(PropertyModel, CancelDiscardsPending)
{
    cd::editor::PropertyModel m;
    m.define("opacity", 1.0F);
    m.begin_edit("opacity", 0.3F);
    m.cancel("opacity");
    EXPECT_FALSE(m.has_pending("opacity"));
    EXPECT_FLOAT_EQ(m.get<float>("opacity").value_or(-1.0F), 1.0F);
}

TEST(PropertyModel, CancelUnknownKeyIsNoOp)
{
    cd::editor::PropertyModel m;
    m.cancel("ghost");  // must not crash
}

TEST(PropertyModel, SetClearsPendingIfAny)
{
    cd::editor::PropertyModel m;
    m.define("scale", 1.0F);
    m.begin_edit("scale", 5.0F);
    ASSERT_TRUE(m.has_pending("scale"));
    m.set("scale", 2.0F);  // direct override
    EXPECT_FALSE(m.has_pending("scale"));
    EXPECT_FLOAT_EQ(m.get<float>("scale").value_or(0.0F), 2.0F);
}

TEST(PropertyModel, MultiplePropertiesIndependent)
{
    cd::editor::PropertyModel m;
    m.define("x", 1.0F);
    m.define("y", 2.0F);
    m.define("z", 3.0F);
    EXPECT_EQ(m.size(), 3U);

    m.begin_edit("x", 10.0F);
    EXPECT_TRUE(m.has_pending("x"));
    EXPECT_FALSE(m.has_pending("y"));
    EXPECT_FALSE(m.has_pending("z"));

    m.cancel("x");
    EXPECT_FALSE(m.has_pending("x"));
}

TEST(PropertyModel, Vec3fRoundTrip)
{
    cd::editor::PropertyModel m;
    const cd::math::Vec3f initial { 1.0F, 2.0F, 3.0F };
    m.define("position", initial);
    m.begin_edit("position", cd::math::Vec3f { 4.0F, 5.0F, 6.0F });
    const auto result = m.commit("position");
    ASSERT_TRUE(result.has_value());
    const auto* after = std::get_if<cd::math::Vec3f>(&result->after);
    ASSERT_NE(after, nullptr);
    EXPECT_FLOAT_EQ(after->x, 4.0F);
    EXPECT_FLOAT_EQ(after->y, 5.0F);
    EXPECT_FLOAT_EQ(after->z, 6.0F);
}

TEST(PropertyModel, Int64AndDoubleAndBool)
{
    cd::editor::PropertyModel m;
    m.define("count", std::int64_t { 7 });
    m.define("weight", 3.14);
    m.define("visible", true);

    EXPECT_EQ(m.get<std::int64_t>("count").value_or(0), 7);
    EXPECT_DOUBLE_EQ(m.get<double>("weight").value_or(0.0), 3.14);
    EXPECT_TRUE(m.get<bool>("visible").value_or(false));
}

TEST(PropertyModel, ClearRemovesAll)
{
    cd::editor::PropertyModel m;
    m.define("a", 1.0F);
    m.define("b", 2.0F);
    m.clear();
    EXPECT_EQ(m.size(), 0U);
    EXPECT_FALSE(m.contains("a"));
}

TEST(PropertyModel, DoubleCommitIsNoOp)
{
    cd::editor::PropertyModel m;
    m.define("x", 0.0F);
    m.begin_edit("x", 5.0F);
    ASSERT_TRUE(m.commit("x").has_value());
    EXPECT_FALSE(m.commit("x").has_value());  // nothing pending → nullopt
}

TEST(PropertyModel, GetPendingTypeMismatchReturnsNullopt)
{
    cd::editor::PropertyModel m;
    m.define("flag", false);
    m.begin_edit("flag", false);  // bool pending
    EXPECT_FALSE(m.get_pending<float>("flag").has_value());  // type mismatch
}

// ===========================================================================
// SelectionOutline
// ===========================================================================

TEST(SelectionOutline, EmptyByDefault)
{
    const cd::editor::SelectionOutline so;
    EXPECT_TRUE(so.empty());
    EXPECT_EQ(so.size(), 0U);
}

TEST(SelectionOutline, SetReplacesAll)
{
    cd::editor::SelectionOutline so;
    const cd::ecs::Entity a { 1, 1 };
    const cd::ecs::Entity b { 2, 1 };
    so.add(a);
    so.set(b);
    EXPECT_EQ(so.size(), 1U);
    EXPECT_TRUE(so.contains(b));
    EXPECT_FALSE(so.contains(a));
}

TEST(SelectionOutline, SetInvalidEntityClearsAll)
{
    cd::editor::SelectionOutline so;
    so.add(cd::ecs::Entity { 5, 1 });
    so.set(cd::ecs::Entity {});  // invalid
    EXPECT_TRUE(so.empty());
}

TEST(SelectionOutline, AddMultipleAndContains)
{
    cd::editor::SelectionOutline so;
    const cd::ecs::Entity a { 10, 1 };
    const cd::ecs::Entity b { 20, 1 };
    const cd::ecs::Entity c { 30, 1 };
    so.add(a);
    so.add(b);
    so.add(c);
    EXPECT_EQ(so.size(), 3U);
    EXPECT_TRUE(so.contains(a));
    EXPECT_TRUE(so.contains(b));
    EXPECT_TRUE(so.contains(c));
}

TEST(SelectionOutline, AddDuplicateIsNoOp)
{
    cd::editor::SelectionOutline so;
    const cd::ecs::Entity a { 7, 1 };
    so.add(a);
    so.add(a);
    EXPECT_EQ(so.size(), 1U);
}

TEST(SelectionOutline, AddInvalidEntityIsNoOp)
{
    cd::editor::SelectionOutline so;
    so.add(cd::ecs::Entity {});  // invalid — must not crash
    EXPECT_TRUE(so.empty());
}

TEST(SelectionOutline, RemoveDropsEntity)
{
    cd::editor::SelectionOutline so;
    const cd::ecs::Entity a { 1, 1 };
    const cd::ecs::Entity b { 2, 1 };
    so.add(a);
    so.add(b);
    so.remove(a);
    EXPECT_FALSE(so.contains(a));
    EXPECT_TRUE(so.contains(b));
    EXPECT_EQ(so.size(), 1U);
}

TEST(SelectionOutline, RemoveAbsentEntityIsNoOp)
{
    cd::editor::SelectionOutline so;
    const cd::ecs::Entity a { 3, 1 };
    so.add(a);
    so.remove(cd::ecs::Entity { 99, 1 });  // not present
    EXPECT_EQ(so.size(), 1U);
    EXPECT_TRUE(so.contains(a));
}

TEST(SelectionOutline, ClearEmptiesAll)
{
    cd::editor::SelectionOutline so;
    so.add(cd::ecs::Entity { 1, 1 });
    so.add(cd::ecs::Entity { 2, 1 });
    so.clear();
    EXPECT_TRUE(so.empty());
    EXPECT_EQ(so.size(), 0U);
}

TEST(SelectionOutline, DefaultVisualParams)
{
    const cd::editor::SelectionOutline so;
    EXPECT_EQ(so.style, cd::editor::OutlineStyle::kStencilEdge);
    EXPECT_GT(so.thickness, 0.0F);
    EXPECT_FLOAT_EQ(so.opacity, 1.0F);
}

TEST(SelectionOutline, ClampParamsEnforcesRange)
{
    cd::editor::SelectionOutline so;
    so.thickness = -5.0F;
    so.opacity   = 3.0F;
    so.clamp_params();
    EXPECT_FLOAT_EQ(so.thickness, 0.0F);
    EXPECT_FLOAT_EQ(so.opacity,   1.0F);

    so.thickness = 100.0F;
    so.opacity   = -1.0F;
    so.clamp_params();
    EXPECT_FLOAT_EQ(so.thickness, 16.0F);
    EXPECT_FLOAT_EQ(so.opacity,   0.0F);
}

TEST(SelectionOutline, EntitiesVectorReflectsAddOrder)
{
    cd::editor::SelectionOutline so;
    const cd::ecs::Entity a { 1, 1 };
    const cd::ecs::Entity b { 2, 1 };
    so.add(a);
    so.add(b);
    const auto& ents = so.entities();
    ASSERT_EQ(ents.size(), 2U);
    EXPECT_EQ(ents[0], a);
    EXPECT_EQ(ents[1], b);
}

// ===========================================================================
// DragSession (PropertyDrawer.hpp)
// ===========================================================================

namespace
{
/// Fake ImGui item-state callbacks for headless DragSession testing.
struct FakeItemState
{
    bool activated              { false };
    bool deactivated_after_edit { false };

    [[nodiscard]] bool is_activated() const noexcept { return activated; }
    [[nodiscard]] bool is_deactivated() const noexcept { return deactivated_after_edit; }
};
}  // namespace

TEST(DragSession, NotArmedByDefault)
{
    cd::editor::DragSessionFloat s;
    EXPECT_FALSE(s.is_active());
    EXPECT_FALSE(s.release_pending);
}

TEST(DragSession, ObserveArmsOnActivated)
{
    cd::editor::DragSessionFloat s;
    FakeItemState st;
    st.activated = true;
    s.observe(5.0F,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });
    EXPECT_TRUE(s.is_active());
    EXPECT_FLOAT_EQ(s.pre_drag, 5.0F);
}

TEST(DragSession, ObserveSetsReleasePendingOnDeactivate)
{
    cd::editor::DragSessionFloat s;
    FakeItemState st;
    // Arm the session first.
    st.activated = true;
    s.observe(3.0F,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });
    ASSERT_TRUE(s.is_active());

    // Now deactivate (user released the mouse).
    st.activated              = false;
    st.deactivated_after_edit = true;
    s.observe(3.0F,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });
    EXPECT_TRUE(s.release_pending);
    EXPECT_FALSE(s.is_active());
}

TEST(DragSession, ConsumeReleaseReturnsTrueOnce)
{
    cd::editor::DragSessionFloat s;
    FakeItemState st;
    // Arm.
    st.activated = true;
    s.observe(2.0F,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });
    // Release.
    st.activated              = false;
    st.deactivated_after_edit = true;
    s.observe(2.0F,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });

    float pre = 0.0F;
    EXPECT_TRUE(s.consume_release(&pre));
    EXPECT_FLOAT_EQ(pre, 2.0F);            // pre_drag written to out-param
    EXPECT_FALSE(s.consume_release(nullptr)); // second call: nothing pending
}

TEST(DragSession, ConsumeReleaseWithNullptrIsSafe)
{
    cd::editor::DragSessionFloat s;
    FakeItemState st;
    st.activated = true;
    s.observe(1.0F,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });
    st.activated = false; st.deactivated_after_edit = true;
    s.observe(1.0F,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });
    EXPECT_TRUE(s.consume_release(nullptr));  // nullptr out-param must not crash
}

TEST(DragSession, ResetCancelsSession)
{
    cd::editor::DragSessionFloat s;
    FakeItemState st;
    st.activated = true;
    s.observe(9.0F,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });
    ASSERT_TRUE(s.is_active());
    s.reset();
    EXPECT_FALSE(s.is_active());
    EXPECT_FALSE(s.release_pending);
    EXPECT_FLOAT_EQ(s.pre_drag, 0.0F);
}

TEST(DragSession, ObserveWithoutActivationDoesNotArm)
{
    cd::editor::DragSessionFloat s;
    FakeItemState st;
    // Neither activated nor deactivated.
    s.observe(5.0F,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });
    EXPECT_FALSE(s.is_active());
    EXPECT_FALSE(s.release_pending);
}

TEST(DragSession, DeactivationWhileUnarmedIsNoOp)
{
    cd::editor::DragSessionFloat s;
    FakeItemState st;
    st.deactivated_after_edit = true;  // deactivate without prior arm
    s.observe(0.0F,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });
    EXPECT_FALSE(s.release_pending);
}

TEST(DragSession, Vec3SessionRoundTrip)
{
    cd::editor::DragSessionVec3 s;
    FakeItemState st;
    const cd::math::Vec3f start { 1.0F, 2.0F, 3.0F };

    // Arm.
    st.activated = true;
    s.observe(start,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });
    ASSERT_TRUE(s.is_active());
    EXPECT_FLOAT_EQ(s.pre_drag.x, 1.0F);

    // Release.
    st.activated = false; st.deactivated_after_edit = true;
    s.observe(start,
              [&]{ return st.is_activated(); },
              [&]{ return st.is_deactivated(); });

    cd::math::Vec3f pre {};
    EXPECT_TRUE(s.consume_release(&pre));
    EXPECT_FLOAT_EQ(pre.x, 1.0F);
    EXPECT_FLOAT_EQ(pre.y, 2.0F);
    EXPECT_FLOAT_EQ(pre.z, 3.0F);
}

TEST(DragDeltaSignificant, TrueWhenAnyComponentExceedsEpsilon)
{
    const cd::math::Vec3f a { 0.0F, 0.0F, 0.0F };
    const cd::math::Vec3f b { 0.1F, 0.0F, 0.0F };
    EXPECT_TRUE(cd::editor::drag_delta_significant(a, b, 1e-6F));
}

TEST(DragDeltaSignificant, FalseWhenAllComponentsWithinEpsilon)
{
    const cd::math::Vec3f a { 1.0F, 2.0F, 3.0F };
    const cd::math::Vec3f b { 1.0F + 1e-9F, 2.0F, 3.0F };
    EXPECT_FALSE(cd::editor::drag_delta_significant(a, b, 1e-6F));
}

TEST(DragDeltaSignificant, HandleNegativeDelta)
{
    const cd::math::Vec3f a { 5.0F, 5.0F, 5.0F };
    const cd::math::Vec3f b { 5.0F, 4.0F, 5.0F };  // y differs by -1
    EXPECT_TRUE(cd::editor::drag_delta_significant(a, b, 0.5F));
}

// ===========================================================================
// EditHistory — coalescing (push_coalescing)
// ===========================================================================

namespace
{

/// Command that sets a float to a specific value and reverts to a previous.
class FloatSetCmd final : public cd::editor::ICommand
{
public:
    FloatSetCmd(float* target, float new_val, float old_val, std::string lbl = "FloatSet")
        : target_ { target }, new_val_ { new_val }, old_val_ { old_val }, label_ { std::move(lbl) }
    {}
    void apply()  override { *target_ = new_val_; }
    void revert() override { *target_ = old_val_; }
    [[nodiscard]] std::string_view label() const noexcept override { return label_; }
    [[nodiscard]] std::size_t byte_size() const noexcept override { return sizeof(*this); }

private:
    float*      target_;
    float       new_val_;
    float       old_val_;
    std::string label_;
};

}  // namespace

TEST(EditHistoryCoalescing, FreshPushWhenNoMatch)
{
    cd::editor::EditHistory hist;
    float val = 0.0F;
    const bool merged = hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 1.0F, 0.0F));
    EXPECT_FALSE(merged);
    EXPECT_FLOAT_EQ(val, 1.0F);
    EXPECT_EQ(hist.undo_depth(), 1U);
}

TEST(EditHistoryCoalescing, SameKeyMerges)
{
    cd::editor::EditHistory hist;
    float val = 0.0F;
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 1.0F, 0.0F));
    const bool merged = hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 2.0F, 1.0F));
    EXPECT_TRUE(merged);
    EXPECT_FLOAT_EQ(val, 2.0F);
    // Only ONE undo entry despite two pushes.
    EXPECT_EQ(hist.undo_depth(), 1U);
}

TEST(EditHistoryCoalescing, UndoRevertsEntireMergedGroup)
{
    cd::editor::EditHistory hist;
    float val = 0.0F;
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 1.0F, 0.0F));
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 2.0F, 1.0F));
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 5.0F, 2.0F));
    ASSERT_EQ(hist.undo_depth(), 1U);
    EXPECT_FLOAT_EQ(val, 5.0F);

    EXPECT_TRUE(hist.undo());
    EXPECT_FLOAT_EQ(val, 0.0F);  // all three undone in one Ctrl+Z
    EXPECT_FALSE(hist.can_undo());
}

TEST(EditHistoryCoalescing, RedoReappliesEntireMergedGroup)
{
    cd::editor::EditHistory hist;
    float val = 0.0F;
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 1.0F, 0.0F));
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 3.0F, 1.0F));
    ASSERT_EQ(hist.undo_depth(), 1U);

    hist.undo();
    ASSERT_FLOAT_EQ(val, 0.0F);
    hist.redo();
    EXPECT_FLOAT_EQ(val, 3.0F);
}

TEST(EditHistoryCoalescing, DifferentKeyStartsNewEntry)
{
    cd::editor::EditHistory hist;
    float x = 0.0F;
    float y = 0.0F;
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&x, 1.0F, 0.0F));
    hist.push_coalescing("e42/pos.y",  // different property
        std::make_unique<FloatSetCmd>(&y, 2.0F, 0.0F));
    EXPECT_EQ(hist.undo_depth(), 2U);
}

TEST(EditHistoryCoalescing, NullCommandIsNoOp)
{
    cd::editor::EditHistory hist;
    const bool merged = hist.push_coalescing("key", nullptr);
    EXPECT_FALSE(merged);
    EXPECT_EQ(hist.undo_depth(), 0U);
}

TEST(EditHistoryCoalescing, PlainPushBreaksCoalesceChain)
{
    cd::editor::EditHistory hist;
    float val = 0.0F;
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 1.0F, 0.0F));
    // plain push resets the coalesce key
    hist.push(std::make_unique<FloatSetCmd>(&val, 2.0F, 1.0F));
    // This coalescing push should NOT merge with the pre-plain-push entry.
    const bool merged = hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 3.0F, 2.0F));
    EXPECT_FALSE(merged);
    EXPECT_EQ(hist.undo_depth(), 3U);
}

TEST(EditHistoryCoalescing, UndoBreaksCoalesceChain)
{
    cd::editor::EditHistory hist;
    float val = 0.0F;
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 1.0F, 0.0F));
    hist.undo();  // resets coalesce key
    // next push_coalescing with same key must NOT merge with the undone entry.
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 2.0F, 0.0F));
    // We had 1 entry, undo moved it to redo, new push adds 1 fresh entry.
    EXPECT_EQ(hist.undo_depth(), 1U);
}

TEST(EditHistoryCoalescing, ClearBreaksCoalesceChain)
{
    cd::editor::EditHistory hist;
    float val = 0.0F;
    hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 1.0F, 0.0F));
    hist.clear();
    const bool merged = hist.push_coalescing("e42/pos.x",
        std::make_unique<FloatSetCmd>(&val, 2.0F, 0.0F));
    EXPECT_FALSE(merged);
    EXPECT_EQ(hist.undo_depth(), 1U);
}

TEST(EditHistoryCoalescing, RedoClearedAfterCoalescingPush)
{
    cd::editor::EditHistory hist;
    float val = 0.0F;
    hist.push_coalescing("k",
        std::make_unique<FloatSetCmd>(&val, 1.0F, 0.0F));
    hist.undo();
    ASSERT_TRUE(hist.can_redo());
    hist.push_coalescing("k",
        std::make_unique<FloatSetCmd>(&val, 1.5F, 0.0F));
    EXPECT_FALSE(hist.can_redo());
}
