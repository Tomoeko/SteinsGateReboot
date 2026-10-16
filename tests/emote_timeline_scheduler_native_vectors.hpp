#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

struct NativeTimelineWrite {
	std::string_view Name;
	std::uint32_t Value = 0;
	std::uint32_t Duration = 0;
	std::uint32_t Easing = 0;
};
struct NativeTimelinePhase {
	std::uint32_t Delta = 0;
	bool Active = false;
	std::uint32_t Position = 0;
	bool Looped = false;
	std::vector<NativeTimelineWrite> Writes;
};
struct NativeTimelineCase {
	float LoopBegin = 0;
	float LoopEnd = 0;
	float LastTime = 0;
	std::uint32_t Flags = 0;
	std::vector<NativeTimelineWrite> Initial;
	std::vector<NativeTimelinePhase> Phases;
};
// Exact original2ccc50 operands/state; these are expected-only, never injected into the actual scheduler.
static const std::vector<NativeTimelineCase> kNativeTimelineCases{
    {-1.0f, 10.0f, 10.0f, 0U, {{"continuous", 1084227584U, 1065353216U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                                                                             {0U, true, 0U, false, {}},
                                                                                                                             {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}}},
                                                                                                                             {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                             {1095237632U, false, 1099694080U, false, {{"continuous", 1103626240U, 0U, 0U}, {"instant", 1104150528U, 0U, 0U}}},
                                                                                                                             {1065353216U, false, 1099694080U, false, {}},
                                                                                                                         }},
    {0.0f, 10.0f, 10.0f, 0U, {{"continuous", 1084227584U, 1065353216U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                                                                            {0U, true, 0U, false, {}},
                                                                                                                            {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}}},
                                                                                                                            {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                            {1095237632U, true, 1089470464U, true, {{"continuous", 1103626240U, 0U, 0U}, {"instant", 1104150528U, 0U, 0U}, {"continuous", 1084227584U, 1065353216U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                            {1065353216U, true, 1091043328U, true, {{"continuous", 1103626240U, 1056964608U, 0U}, {"instant", 1104150528U, 1056964608U, 0U}}},
                                                                                                                        }},
    {2.0f, 8.0f, 10.0f, 0U, {{"continuous", 1084227584U, 1065353216U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                                                                           {0U, true, 0U, false, {}},
                                                                                                                           {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}}},
                                                                                                                           {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                           {1095237632U, true, 1085276160U, true, {{"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                           {1065353216U, true, 1087373312U, true, {}},
                                                                                                                       }},
    {1.25f, 7.75f, 10.0f, 0U, {{"continuous", 1084227584U, 1065353216U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                                                                             {0U, true, 0U, false, {}},
                                                                                                                             {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}}},
                                                                                                                             {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                             {1095237632U, true, 1083179008U, true, {{"continuous", 1084227584U, 0U, 0U}, {"instant", 1086324736U, 0U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"continuous", 1084227584U, 0U, 0U}, {"instant", 1086324736U, 0U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 1056964608U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 1056964608U, 0U}}},
                                                                                                                             {1065353216U, true, 1085276160U, true, {}},
                                                                                                                         }},
    {-1.0f, 10.0f, 10.0f, 1U, {{"continuous", 1084227584U, 1065353216U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                                                                             {0U, true, 0U, false, {}},
                                                                                                                             {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}}},
                                                                                                                             {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                             {1095237632U, false, 1099694080U, false, {{"continuous", 1103626240U, 0U, 0U}, {"instant", 1104150528U, 0U, 0U}}},
                                                                                                                             {1065353216U, false, 1099694080U, false, {}},
                                                                                                                         }},
    {0.0f, 10.0f, 10.0f, 1U, {{"continuous", 1084227584U, 1065353216U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                                                                            {0U, true, 0U, false, {}},
                                                                                                                            {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}}},
                                                                                                                            {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                            {1095237632U, true, 1089470464U, true, {{"continuous", 1103626240U, 0U, 0U}, {"instant", 1104150528U, 0U, 0U}, {"continuous", 1084227584U, 1065353216U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                            {1065353216U, true, 1091043328U, true, {{"continuous", 1103626240U, 1056964608U, 0U}, {"instant", 1104150528U, 1056964608U, 0U}}},
                                                                                                                        }},
    {2.0f, 8.0f, 10.0f, 1U, {{"continuous", 1084227584U, 1065353216U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                                                                           {0U, true, 0U, false, {}},
                                                                                                                           {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}}},
                                                                                                                           {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                           {1095237632U, true, 1085276160U, true, {{"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                           {1065353216U, true, 1087373312U, true, {}},
                                                                                                                       }},
    {1.25f, 7.75f, 10.0f, 1U, {{"continuous", 1084227584U, 1065353216U, 0U}, {"instant", 1086324736U, 1065353216U, 0U}}, {
                                                                                                                             {0U, true, 0U, false, {}},
                                                                                                                             {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}, {"instant", 1093664768U, 1065353216U, 0U}}},
                                                                                                                             {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}}},
                                                                                                                             {1095237632U, true, 1083179008U, true, {{"continuous", 1084227584U, 0U, 0U}, {"instant", 1086324736U, 0U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 0U, 0U}, {"continuous", 1084227584U, 0U, 0U}, {"instant", 1086324736U, 0U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 1056964608U, 0U}, {"instant", 1093664768U, 0U, 0U}, {"instant", 1098907648U, 1056964608U, 0U}}},
                                                                                                                             {1065353216U, true, 1085276160U, true, {}},
                                                                                                                         }},
    {-1.0f, 10.0f, 10.0f, 4U, {{"continuous", 1084227584U, 1065353216U, 0U}}, {
                                                                                  {0U, true, 0U, false, {}},
                                                                                  {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}}},
                                                                                  {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}}},
                                                                                  {1095237632U, false, 1099694080U, false, {{"continuous", 1103626240U, 0U, 0U}}},
                                                                                  {1065353216U, false, 1099694080U, false, {}},
                                                                              }},
    {0.0f, 10.0f, 10.0f, 4U, {{"continuous", 1084227584U, 1065353216U, 0U}}, {
                                                                                 {0U, true, 0U, false, {}},
                                                                                 {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}}},
                                                                                 {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}}},
                                                                                 {1095237632U, true, 1089470464U, true, {{"continuous", 1103626240U, 0U, 0U}, {"continuous", 1084227584U, 1065353216U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 0U, 0U}}},
                                                                                 {1065353216U, true, 1091043328U, true, {{"continuous", 1103626240U, 1056964608U, 0U}}},
                                                                             }},
    {2.0f, 8.0f, 10.0f, 4U, {{"continuous", 1084227584U, 1065353216U, 0U}}, {
                                                                                {0U, true, 0U, false, {}},
                                                                                {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}}},
                                                                                {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}}},
                                                                                {1095237632U, true, 1085276160U, true, {{"continuous", 1092616192U, 1065353216U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"continuous", 1092616192U, 1065353216U, 0U}, {"continuous", 1097859072U, 0U, 0U}}},
                                                                                {1065353216U, true, 1087373312U, true, {}},
                                                                            }},
    {1.25f, 7.75f, 10.0f, 4U, {{"continuous", 1084227584U, 1065353216U, 0U}}, {
                                                                                  {0U, true, 0U, false, {}},
                                                                                  {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}}},
                                                                                  {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}}},
                                                                                  {1095237632U, true, 1083179008U, true, {{"continuous", 1084227584U, 0U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"continuous", 1084227584U, 0U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 1056964608U, 0U}}},
                                                                                  {1065353216U, true, 1085276160U, true, {}},
                                                                              }},
    {-1.0f, 10.0f, 10.0f, 5U, {{"continuous", 1084227584U, 1065353216U, 0U}}, {
                                                                                  {0U, true, 0U, false, {}},
                                                                                  {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}}},
                                                                                  {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}}},
                                                                                  {1095237632U, false, 1099694080U, false, {{"continuous", 1103626240U, 0U, 0U}}},
                                                                                  {1065353216U, false, 1099694080U, false, {}},
                                                                              }},
    {0.0f, 10.0f, 10.0f, 5U, {{"continuous", 1084227584U, 1065353216U, 0U}}, {
                                                                                 {0U, true, 0U, false, {}},
                                                                                 {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}}},
                                                                                 {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}}},
                                                                                 {1095237632U, true, 1089470464U, true, {{"continuous", 1103626240U, 0U, 0U}, {"continuous", 1084227584U, 1065353216U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 0U, 0U}}},
                                                                                 {1065353216U, true, 1091043328U, true, {{"continuous", 1103626240U, 1056964608U, 0U}}},
                                                                             }},
    {2.0f, 8.0f, 10.0f, 5U, {{"continuous", 1084227584U, 1065353216U, 0U}}, {
                                                                                {0U, true, 0U, false, {}},
                                                                                {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}}},
                                                                                {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}}},
                                                                                {1095237632U, true, 1085276160U, true, {{"continuous", 1092616192U, 1065353216U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"continuous", 1092616192U, 1065353216U, 0U}, {"continuous", 1097859072U, 0U, 0U}}},
                                                                                {1065353216U, true, 1087373312U, true, {}},
                                                                            }},
    {1.25f, 7.75f, 10.0f, 5U, {{"continuous", 1084227584U, 1065353216U, 0U}}, {
                                                                                  {0U, true, 0U, false, {}},
                                                                                  {1073741824U, true, 1073741824U, false, {{"continuous", 1092616192U, 1065353216U, 0U}}},
                                                                                  {1077936128U, true, 1084227584U, false, {{"continuous", 1097859072U, 0U, 0U}}},
                                                                                  {1095237632U, true, 1083179008U, true, {{"continuous", 1084227584U, 0U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 0U, 0U}, {"continuous", 1084227584U, 0U, 0U}, {"continuous", 1092616192U, 0U, 0U}, {"continuous", 1097859072U, 1056964608U, 0U}}},
                                                                                  {1065353216U, true, 1085276160U, true, {}},
                                                                              }},
};
