#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

struct NativeAdditiveWrite {
	std::string_view Name;
	std::uint32_t Value = 0;
	std::uint32_t Duration = 0;
	std::uint32_t Easing = 0;
};
struct NativeAdditivePhase {
	std::uint32_t Delta = 0;
	bool Active = false;
	std::uint32_t Position = 0;
	bool Looped = false;
	std::uint32_t Blend = 0;
	std::uint32_t AddedToSeven = 0;
	bool HasFade = false;
	std::uint32_t FadeTarget = 0;
	std::uint32_t FadeDuration = 0;
	bool Stop = false;
	std::vector<NativeAdditiveWrite> Writes;
};
struct NativeAdditiveCase {
	float LoopBegin = 0;
	float LoopEnd = 0;
	float LastTime = 0;
	std::uint32_t Flags = 0;
	std::vector<NativeAdditiveWrite> Initial;
	std::vector<NativeAdditivePhase> Phases;
};
// Expected-only original2ccc50/2ccf90/2ce4d0/2baf30 state and real ordinary SetVariable boundary operands.
static const std::vector<NativeAdditiveCase> kNativeAdditiveCases{
    {-1.0f, 10.0f, 10.0f, 2U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                               {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                               {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                               {1073741824U, true, 1077936128U, false, 1065353216U, 1099431936U, false, 0, 0, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                               {1077936128U, true, 1086324736U, false, 1065353216U, 1102053376U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                               {1095237632U, false, 1100218368U, false, 1065353216U, 1088421888U, false, 0, 0, false, {{"instant", 1104150528U, 0U, 0U}}},
                                                                               {1065353216U, false, 1100218368U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                           }},
    {0.0f, 10.0f, 10.0f, 2U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                              {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                              {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                              {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                              {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                              {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                              {1095237632U, true, 1091043328U, true, 1065353216U, 1102053376U, false, 0, 0, false, {{"instant", 1104150528U, 0U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"instant", 1104150528U, 1056964608U, 0U}}},
                                                                              {1065353216U, true, 1092091904U, true, 1065353216U, 1107296256U, false, 0, 0, false, {}},
                                                                          }},
    {2.0f, 8.0f, 10.0f, 2U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                             {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                             {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                             {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                             {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                             {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                             {1095237632U, true, 1087373312U, true, 1065353216U, 1102053376U, false, 0, 0, false, {{"instant", 1093664768U, 1065353216U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                             {1065353216U, true, 1089470464U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                                                         }},
    {1.25f, 7.75f, 10.0f, 2U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                               {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                               {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                               {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                               {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                               {1095237632U, true, 1085276160U, true, 1065353216U, 1102053376U, false, 0, 0, false, {{"instant", 1086324736U, 0U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"instant", 1086324736U, 0U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                               {1065353216U, true, 1087373312U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                                                           }},
    {-1.0f, 10.0f, 10.0f, 3U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                               {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                               {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                               {1073741824U, true, 1077936128U, false, 1065353216U, 1099431936U, false, 0, 0, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                               {1077936128U, true, 1086324736U, false, 1065353216U, 1102053376U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                               {1095237632U, false, 1100218368U, false, 1065353216U, 1088421888U, false, 0, 0, false, {{"instant", 1104150528U, 0U, 0U}}},
                                                                               {1065353216U, false, 1100218368U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                           }},
    {0.0f, 10.0f, 10.0f, 3U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                              {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                              {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                              {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                              {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                              {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                              {1095237632U, true, 1091043328U, true, 1065353216U, 1102053376U, false, 0, 0, false, {{"instant", 1104150528U, 0U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"instant", 1104150528U, 1056964608U, 0U}}},
                                                                              {1065353216U, true, 1092091904U, true, 1065353216U, 1107296256U, false, 0, 0, false, {}},
                                                                          }},
    {2.0f, 8.0f, 10.0f, 3U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                             {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                             {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                             {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                             {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                             {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                             {1095237632U, true, 1087373312U, true, 1065353216U, 1102053376U, false, 0, 0, false, {{"instant", 1093664768U, 1065353216U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                             {1065353216U, true, 1089470464U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                                                         }},
    {1.25f, 7.75f, 10.0f, 3U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                               {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                               {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                               {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                               {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                               {1095237632U, true, 1085276160U, true, 1065353216U, 1102053376U, false, 0, 0, false, {{"instant", 1086324736U, 0U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"instant", 1086324736U, 0U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                               {1065353216U, true, 1087373312U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                                                           }},
    {-1.0f, 10.0f, 10.0f, 6U, {}, {
                                      {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                      {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                      {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                      {1073741824U, true, 1077936128U, false, 1065353216U, 1099431936U, false, 0, 0, false, {}},
                                      {1077936128U, true, 1086324736U, false, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                      {1095237632U, false, 1100218368U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                      {1065353216U, false, 1100218368U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                  }},
    {0.0f, 10.0f, 10.0f, 6U, {}, {
                                     {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                     {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                     {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                     {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                     {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {}},
                                     {1095237632U, true, 1091043328U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                     {1065353216U, true, 1092091904U, true, 1065353216U, 1107296256U, false, 0, 0, false, {}},
                                 }},
    {2.0f, 8.0f, 10.0f, 6U, {}, {
                                    {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                    {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                    {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                    {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                    {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {}},
                                    {1095237632U, true, 1087373312U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                    {1065353216U, true, 1089470464U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                }},
    {1.25f, 7.75f, 10.0f, 6U, {}, {
                                      {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                      {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                      {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                      {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                      {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {}},
                                      {1095237632U, true, 1085276160U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                      {1065353216U, true, 1087373312U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                  }},
    {-1.0f, 10.0f, 10.0f, 7U, {}, {
                                      {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                      {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                      {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                      {1073741824U, true, 1077936128U, false, 1065353216U, 1099431936U, false, 0, 0, false, {}},
                                      {1077936128U, true, 1086324736U, false, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                      {1095237632U, false, 1100218368U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                      {1065353216U, false, 1100218368U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                  }},
    {0.0f, 10.0f, 10.0f, 7U, {}, {
                                     {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                     {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                     {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                     {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                     {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {}},
                                     {1095237632U, true, 1091043328U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                     {1065353216U, true, 1092091904U, true, 1065353216U, 1107296256U, false, 0, 0, false, {}},
                                 }},
    {2.0f, 8.0f, 10.0f, 7U, {}, {
                                    {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                    {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                    {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                    {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                    {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {}},
                                    {1095237632U, true, 1087373312U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                    {1065353216U, true, 1089470464U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                }},
    {1.25f, 7.75f, 10.0f, 7U, {}, {
                                      {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                      {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                      {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                      {1073741824U, true, 1077936128U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                      {1077936128U, true, 1086324736U, false, 1065353216U, 1099431936U, false, 0, 0, false, {}},
                                      {1095237632U, true, 1085276160U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                      {1065353216U, true, 1087373312U, true, 1065353216U, 1102053376U, false, 0, 0, false, {}},
                                  }},
    {-1.0f, 10.0f, 10.0f, 2U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                               {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                               {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                               {1073741824U, true, 1077936128U, false, 0U, 1088421888U, true, 0U, 1069547520U, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                               {1077936128U, true, 1086324736U, false, 0U, 1088421888U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                               {1095237632U, false, 1100218368U, false, 0U, 1088421888U, false, 0, 0, false, {{"instant", 1104150528U, 0U, 0U}}},
                                                                               {1065353216U, false, 1100218368U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                                                           }},
    {0.0f, 10.0f, 10.0f, 2U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                              {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                              {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                              {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                              {1073741824U, true, 1077936128U, false, 1048576000U, 1090781184U, true, 1048576000U, 1069547520U, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                              {1077936128U, true, 1086324736U, false, 1048576000U, 1092091904U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                              {1095237632U, true, 1091043328U, true, 1048576000U, 1093402624U, false, 0, 0, false, {{"instant", 1104150528U, 0U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"instant", 1104150528U, 1056964608U, 0U}}},
                                                                              {1065353216U, true, 1092091904U, true, 1048576000U, 1096024064U, false, 0, 0, false, {}},
                                                                          }},
    {2.0f, 8.0f, 10.0f, 2U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                             {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                             {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                             {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                             {1073741824U, true, 1077936128U, false, 0U, 1088421888U, true, 0U, 0U, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                             {1077936128U, true, 1086324736U, false, 0U, 1088421888U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                             {1095237632U, true, 1087373312U, true, 0U, 1088421888U, false, 0, 0, false, {{"instant", 1093664768U, 1065353216U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                             {1065353216U, true, 1089470464U, true, 0U, 1088421888U, false, 0, 0, false, {}},
                                                                         }},
    {1.25f, 7.75f, 10.0f, 2U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                               {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                               {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                               {1073741824U, true, 1077936128U, false, 1048576000U, 1090781184U, true, 1048576000U, 0U, false, {{"instant", 1093664768U, 0U, 0U}}},
                                                                               {1077936128U, true, 1086324736U, false, 1048576000U, 1092091904U, false, 0, 0, false, {{"instant", 1098907648U, 0U, 0U}}},
                                                                               {1095237632U, true, 1085276160U, true, 1048576000U, 1093402624U, false, 0, 0, false, {{"instant", 1086324736U, 0U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"instant", 1086324736U, 0U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                               {1065353216U, true, 1087373312U, true, 1048576000U, 1093402624U, false, 0, 0, false, {}},
                                                                           }},
    {-1.0f, 10.0f, 10.0f, 3U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                               {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                               {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                               {1073741824U, false, 1077936128U, false, 0U, 1088421888U, true, 0U, 1069547520U, true, {{"instant", 1093664768U, 0U, 0U}}},
                                                                               {1077936128U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1095237632U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1065353216U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                                                           }},
    {0.0f, 10.0f, 10.0f, 3U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                              {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                              {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                              {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                              {1073741824U, false, 1077936128U, false, 1048576000U, 1088421888U, true, 1048576000U, 1069547520U, true, {{"instant", 1093664768U, 0U, 0U}}},
                                                                              {1077936128U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                                                              {1095237632U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                                                              {1065353216U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                                                          }},
    {2.0f, 8.0f, 10.0f, 3U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                             {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                             {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                             {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                             {1073741824U, false, 1077936128U, false, 0U, 1088421888U, true, 0U, 0U, true, {{"instant", 1093664768U, 0U, 0U}}},
                                                                             {1077936128U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                                                             {1095237632U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                                                             {1065353216U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                                                         }},
    {1.25f, 7.75f, 10.0f, 3U, {{"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                               {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                                                               {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                                                               {1073741824U, false, 1077936128U, false, 1048576000U, 1088421888U, true, 1048576000U, 0U, true, {{"instant", 1093664768U, 0U, 0U}}},
                                                                               {1077936128U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1095237632U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                                                               {1065353216U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                                                           }},
    {-1.0f, 10.0f, 10.0f, 6U, {}, {
                                      {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                      {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                      {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                      {1073741824U, true, 1077936128U, false, 0U, 1088421888U, true, 0U, 1069547520U, false, {}},
                                      {1077936128U, true, 1086324736U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                      {1095237632U, false, 1100218368U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                      {1065353216U, false, 1100218368U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                  }},
    {0.0f, 10.0f, 10.0f, 6U, {}, {
                                     {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                     {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                     {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                     {1073741824U, true, 1077936128U, false, 1048576000U, 1090781184U, true, 1048576000U, 1069547520U, false, {}},
                                     {1077936128U, true, 1086324736U, false, 1048576000U, 1092091904U, false, 0, 0, false, {}},
                                     {1095237632U, true, 1091043328U, true, 1048576000U, 1093402624U, false, 0, 0, false, {}},
                                     {1065353216U, true, 1092091904U, true, 1048576000U, 1096024064U, false, 0, 0, false, {}},
                                 }},
    {2.0f, 8.0f, 10.0f, 6U, {}, {
                                    {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                    {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                    {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                    {1073741824U, true, 1077936128U, false, 0U, 1088421888U, true, 0U, 0U, false, {}},
                                    {1077936128U, true, 1086324736U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                    {1095237632U, true, 1087373312U, true, 0U, 1088421888U, false, 0, 0, false, {}},
                                    {1065353216U, true, 1089470464U, true, 0U, 1088421888U, false, 0, 0, false, {}},
                                }},
    {1.25f, 7.75f, 10.0f, 6U, {}, {
                                      {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                      {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                      {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                      {1073741824U, true, 1077936128U, false, 1048576000U, 1090781184U, true, 1048576000U, 0U, false, {}},
                                      {1077936128U, true, 1086324736U, false, 1048576000U, 1092091904U, false, 0, 0, false, {}},
                                      {1095237632U, true, 1085276160U, true, 1048576000U, 1093402624U, false, 0, 0, false, {}},
                                      {1065353216U, true, 1087373312U, true, 1048576000U, 1093402624U, false, 0, 0, false, {}},
                                  }},
    {-1.0f, 10.0f, 10.0f, 7U, {}, {
                                      {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                      {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                      {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                      {1073741824U, false, 1077936128U, false, 0U, 1088421888U, true, 0U, 1069547520U, true, {}},
                                      {1077936128U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                      {1095237632U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                      {1065353216U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                  }},
    {0.0f, 10.0f, 10.0f, 7U, {}, {
                                     {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                     {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                     {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                     {1073741824U, false, 1077936128U, false, 1048576000U, 1088421888U, true, 1048576000U, 1069547520U, true, {}},
                                     {1077936128U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                     {1095237632U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                     {1065353216U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                 }},
    {2.0f, 8.0f, 10.0f, 7U, {}, {
                                    {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                    {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                    {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                    {1073741824U, false, 1077936128U, false, 0U, 1088421888U, true, 0U, 0U, true, {}},
                                    {1077936128U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                    {1095237632U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                    {1065353216U, false, 1077936128U, false, 0U, 1088421888U, false, 0, 0, false, {}},
                                }},
    {1.25f, 7.75f, 10.0f, 7U, {}, {
                                      {0U, true, 0U, false, 1065353216U, 1088421888U, false, 0, 0, false, {}},
                                      {1048576000U, true, 1048576000U, false, 1065353216U, 1090781184U, false, 0, 0, false, {}},
                                      {1061158912U, true, 1065353216U, false, 1065353216U, 1094713344U, false, 0, 0, false, {}},
                                      {1073741824U, false, 1077936128U, false, 1048576000U, 1088421888U, true, 1048576000U, 0U, true, {}},
                                      {1077936128U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                      {1095237632U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                      {1065353216U, false, 1077936128U, false, 1048576000U, 1088421888U, false, 0, 0, false, {}},
                                  }},
};
