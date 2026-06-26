#include "hardware/AIServerProfile.hpp"
#include "tests/TestHelpers.hpp"

#include <gtest/gtest.h>

namespace openbmc::tests
{

/*
 * 驗證 AIServerProfile 可以從 JSON 解析出硬體數量與重要欄位。
 *
 * 這個測試不驗證策略邏輯，只確認設定檔格式與資料結構轉換能正常運作。
 */
TEST(ProfileParsingTest, ParsesConfiguredComponentCountsAndFields)
{
    const auto profile = hardware::AIServerProfile::fromJson(makeProfileJson());

    EXPECT_EQ(profile.systemPowerBudgetWatts, 1200);
    EXPECT_EQ(profile.gpus.size(), 2U);
    EXPECT_EQ(profile.fans.size(), 2U);
    EXPECT_EQ(profile.psus.size(), 2U);
    EXPECT_EQ(profile.nvmes.size(), 2U);
    EXPECT_EQ(profile.cpus.size(), 2U);
    EXPECT_EQ(profile.gpus.at(0).id, "gpu0");
    EXPECT_DOUBLE_EQ(profile.gpus.at(1).powerWatts, 230.0);
    EXPECT_EQ(profile.cpus.at(0).model, "AMD EPYC 9654");
}

} // 命名空間 openbmc::tests
