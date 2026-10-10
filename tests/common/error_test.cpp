#include "common/error.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace menos::common {
namespace {

TEST(ErrorTest, CarriesMessage) {
    const Error error("boom");
    EXPECT_STREQ(error.what(), "boom");
}

TEST(ErrorTest, IsStdRuntimeError) {
    const Error error("boom");
    const std::runtime_error& base = error;
    EXPECT_STREQ(base.what(), "boom");
}

TEST(InvalidCircuitTest, IsAnError) {
    const InvalidCircuit error("operation 3: qubit index out of range");
    const Error& base = error;
    EXPECT_STREQ(base.what(), "operation 3: qubit index out of range");
}

TEST(UnsupportedFeatureTest, IsAnError) {
    const UnsupportedFeature error("mid-circuit measurement is not supported");
    const Error& base = error;
    EXPECT_STREQ(base.what(), "mid-circuit measurement is not supported");
}

TEST(ResourceExceededTest, IsAnError) {
    const ResourceExceeded error("required 128 bytes, budget is 64 bytes");
    const Error& base = error;
    EXPECT_STREQ(base.what(), "required 128 bytes, budget is 64 bytes");
}

TEST(InternalErrorTest, IsAnError) {
    const InternalError error("invariant violated");
    const Error& base = error;
    EXPECT_STREQ(base.what(), "invariant violated");
}

TEST(EnsureTest, DoesNotThrowWhenConditionHolds) {
    EXPECT_NO_THROW(ensure(true, "unreachable"));
}

TEST(EnsureTest, ThrowsInternalErrorWhenConditionFails) {
    EXPECT_THROW(ensure(false, "invariant violated"), InternalError);
}

TEST(EnsureTest, ThrownErrorCarriesTheMessage) {
    try {
        ensure(false, "invariant violated");
        FAIL() << "ensure() did not throw";
    } catch (const InternalError& error) {
        EXPECT_STREQ(error.what(), "invariant violated");
    }
}

TEST(EnsureTest, ThrownErrorIsCatchableAsBaseError) {
    EXPECT_THROW(ensure(false, "invariant violated"), Error);
}

TEST(EnsureTest, ThrownErrorIsCatchableAsStdException) {
    EXPECT_THROW(ensure(false, "invariant violated"), std::exception);
}

}  // namespace
}  // namespace menos::common
