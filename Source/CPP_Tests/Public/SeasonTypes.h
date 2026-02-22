#pragma once

#include "CoreMinimal.h"
#include "SeasonTypes.generated.h"

UENUM(BlueprintType)
enum class EWorldSeason : uint8
{
	Spring UMETA(DisplayName = "Spring"),
	Summer UMETA(DisplayName = "Summer"),
	Fall UMETA(DisplayName = "Fall"),
	Winter UMETA(DisplayName = "Winter")
};

UENUM(BlueprintType)
enum class ESeasonRegionMode : uint8
{
	Procedural UMETA(DisplayName = "Procedural (Season Override)"),
	Locked UMETA(DisplayName = "Locked (Season + Time)")
};

UENUM(BlueprintType)
enum class ESeasonLockedTimeRule : uint8
{
	FixedTime UMETA(DisplayName = "Fixed Time"),
	OffsetFromDeviceTime UMETA(DisplayName = "Offset From Device Time")
};
