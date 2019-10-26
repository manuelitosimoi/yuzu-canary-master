// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once
#include <optional>
#include <string>
#include "common/common_types.h"
#include "core/hle/service/set/set.h"

namespace Service::NS {
/// This is nn::ns::detail::ApplicationLanguage
enum class ApplicationLanguage : u8 {
    AmericanEnglish = 0,
    BritishEnglish,
    Japanese,
    French,
    German,
    LatinAmericanSpanish,
    Spanish,
    Italian,
    Dutch,
    CanadianFrench,
    Portuguese,
    Russian,
    Korean,
    TraditionalChinese,
    SimplifiedChinese,
    Count
};
using ApplicationLanguagePriorityList =
    const std::array<ApplicationLanguage, static_cast<std::size_t>(ApplicationLanguage::Count)>;

constexpr u32 GetSupportedLanguageFlag(const ApplicationLanguage lang) {
    return 1U << static_cast<u32>(lang);
}

const ApplicationLanguagePriorityList* GetApplicationLanguagePriorityList(ApplicationLanguage lang);
std::optional<ApplicationLanguage> ConvertToApplicationLanguage(
    Service::Set::LanguageCode language_code);
std::optional<Service::Set::LanguageCode> ConvertToLanguageCode(ApplicationLanguage lang);
} // namespace Service::NS