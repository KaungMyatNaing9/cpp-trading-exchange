#include "common/mem_pool.h"

#include <gtest/gtest.h>

namespace {
struct Order {
    int id = 0;
    double price = 0.0;
    Order() = default;
    Order(int i, double p) : id(i), price(p) {}
};
}  // namespace

TEST(MemPoolTest, AllocateConstructsWithArgs) {
    Common::MemPool<Order> pool(4);
    Order *o = pool.allocate(7, 101.5);
    EXPECT_EQ(o->id, 7);
    EXPECT_DOUBLE_EQ(o->price, 101.5);
}

TEST(MemPoolTest, AllocationsAreDistinct) {
    Common::MemPool<Order> pool(3);
    Order *a = pool.allocate(1, 1.0);
    Order *b = pool.allocate(2, 2.0);
    Order *c = pool.allocate(3, 3.0);
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_NE(a, c);
}

TEST(MemPoolTest, DeallocatedSlotIsReused) {
    Common::MemPool<Order> pool(2);
    Order *a = pool.allocate(1, 1.0);
    pool.allocate(2, 2.0);  // pool now full
    pool.deallocate(a);
    Order *c = pool.allocate(3, 3.0);
    EXPECT_EQ(c, a);  // only free slot is the one we released
    EXPECT_EQ(c->id, 3);
}
