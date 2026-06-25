#include "buffer/clock_replacer.h"

#include "gtest/gtest.h"

TEST(CLOCKReplacerTest, BasicVictimTest) {
  CLOCKReplacer clock_replacer(3);

  clock_replacer.Unpin(1);
  clock_replacer.Unpin(2);
  clock_replacer.Unpin(3);
  clock_replacer.Unpin(1);
  EXPECT_EQ(3, clock_replacer.Size());

  int value = -1;
  EXPECT_TRUE(clock_replacer.Victim(&value));
  EXPECT_EQ(1, value);
  EXPECT_EQ(2, clock_replacer.Size());

  EXPECT_TRUE(clock_replacer.Victim(&value));
  EXPECT_EQ(2, value);

  EXPECT_TRUE(clock_replacer.Victim(&value));
  EXPECT_EQ(3, value);

  EXPECT_FALSE(clock_replacer.Victim(&value));
  EXPECT_EQ(0, clock_replacer.Size());
}

TEST(CLOCKReplacerTest, SecondChanceTest) {
  CLOCKReplacer clock_replacer(3);

  clock_replacer.Unpin(1);
  clock_replacer.Unpin(2);
  clock_replacer.Unpin(3);

  int value = -1;
  EXPECT_TRUE(clock_replacer.Victim(&value));
  EXPECT_EQ(1, value);

  clock_replacer.Unpin(2);

  EXPECT_TRUE(clock_replacer.Victim(&value));
  EXPECT_EQ(3, value);

  EXPECT_TRUE(clock_replacer.Victim(&value));
  EXPECT_EQ(2, value);
  EXPECT_FALSE(clock_replacer.Victim(&value));
}

TEST(CLOCKReplacerTest, PinRemovesCandidateTest) {
  CLOCKReplacer clock_replacer(3);

  clock_replacer.Unpin(1);
  clock_replacer.Unpin(2);
  clock_replacer.Pin(1);
  EXPECT_EQ(1, clock_replacer.Size());

  int value = -1;
  EXPECT_TRUE(clock_replacer.Victim(&value));
  EXPECT_EQ(2, value);
  EXPECT_FALSE(clock_replacer.Victim(&value));
}

TEST(CLOCKReplacerTest, CapacityTest) {
  CLOCKReplacer clock_replacer(2);

  clock_replacer.Unpin(1);
  clock_replacer.Unpin(2);
  clock_replacer.Unpin(3);
  EXPECT_EQ(2, clock_replacer.Size());

  int value = -1;
  EXPECT_TRUE(clock_replacer.Victim(&value));
  EXPECT_EQ(1, value);
  EXPECT_TRUE(clock_replacer.Victim(&value));
  EXPECT_EQ(2, value);
  EXPECT_FALSE(clock_replacer.Victim(&value));
}
