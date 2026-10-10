#include "runtime/check_plan_invariants.h"

#include <gtest/gtest.h>

#include "common/error.h"

namespace menos::runtime {
namespace {

// A minimal, correct 2-region plan for a 2-qubit circuit with one CX:
// region 0 = {0}, region 1 = {1} -- deliberately WRONG (the CX spans both
// regions), to prove check_plan_invariants catches a cross-component
// operation rather than silently proceeding (CLAUDE.md/BUILD_PLAN.md
// Phase 3, 3.7's "no silent fallback" guardrail). A real planner/region
// builder would never produce this shape; this test exercises the
// invariant check directly, bypassing them.
Plan make_plan_with_operation_spanning_two_regions() {
    Region region0{.local_to_global = {0},
                   .global_to_local = {0, kNoLocalQubit},
                   .operations = {},
                   .engine = Engine::Dense,
                   .backend = Backend::Cpu,
                   .engine_selection_reason = "",
                   .observable = std::nullopt,
                   .estimated_state_bytes = 32,
                   .estimated_work_units = 0};
    Region region1{.local_to_global = {1},
                   .global_to_local = {kNoLocalQubit, 0},
                   .operations = {},
                   .engine = Engine::Dense,
                   .backend = Backend::Cpu,
                   .engine_selection_reason = "",
                   .observable = std::nullopt,
                   .estimated_state_bytes = 32,
                   .estimated_work_units = 0};
    Plan plan;
    plan.strategy = Strategy::Factorized;
    plan.exact = true;
    plan.regions = {region0, region1};
    plan.estimated_state_bytes = 64;
    plan.estimated_work_units = 0;
    return plan;
}

TEST(CheckPlanInvariantsTest, ValidTwoRegionPlanPasses) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::X, {1});
    // Both operations are single-qubit and each stays within its own
    // region -- a genuinely valid (if minimal) 2-region plan.
    EXPECT_NO_THROW(
        check_plan_invariants(circuit, make_plan_with_operation_spanning_two_regions()));
}

TEST(CheckPlanInvariantsTest, CrossRegionOperationThrowsInternalError) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::CX, {0, 1});  // spans both regions -- invalid for this plan
    EXPECT_THROW(check_plan_invariants(circuit, make_plan_with_operation_spanning_two_regions()),
                 common::InternalError);
}

TEST(CheckPlanInvariantsTest, UncoveredQubitThrowsInternalError) {
    // A plan whose regions don't cover every circuit qubit.
    Region region0{.local_to_global = {0},
                   .global_to_local = {0, kNoLocalQubit},
                   .operations = {},
                   .engine = Engine::Dense,
                   .backend = Backend::Cpu,
                   .engine_selection_reason = "",
                   .observable = std::nullopt,
                   .estimated_state_bytes = 32,
                   .estimated_work_units = 0};
    Plan plan;
    plan.strategy = Strategy::Factorized;
    plan.exact = true;
    plan.regions = {region0};  // qubit 1 covered by nothing

    Circuit circuit(2, 0);
    EXPECT_THROW(check_plan_invariants(circuit, plan), common::InternalError);
}

}  // namespace
}  // namespace menos::runtime
