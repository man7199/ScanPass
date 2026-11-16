#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "InputCoreTypes.h"
#include "Misc/StringFormatArg.h"
#include "FootballHUD.generated.h"

class AFootballDrillManager;
class UBallKickComponent;
class UFont;
struct FFootballDrillHudState;
enum class EFootballDrillMessage : uint8;

/** Language of everything the HUD and the menus show. */
UENUM(BlueprintType)
enum class EFootballHudLanguage : uint8
{
	English,
	Japanese
};

/**
 * Drill HUD, drawn entirely in code so the project needs no widget assets.
 *
 * In play it shows the crosshair and the predicted path of the pass, the power bar while the kick key
 * is held, the lives left (top right), the clock the player is on, the level and the score, and the
 * game over panel.
 *
 * It is also the game's menus: the main menu (start, how to play, exit, and the English / Japanese
 * switch in its top right corner), the how-to-play screen with a keyboard and mouse diagram, and the
 * "back to main menu?" question on Esc. The menus are buttons drawn on the canvas and clicked with the
 * mouse; the game is paused while one is open. The language chosen is remembered between sessions.
 *
 * Text is drawn with fonts that come with Windows: Segoe UI for English, and for Japanese (the engine's
 * own fonts have no Japanese glyphs) Yu Gothic, Meiryo or MS Gothic, whichever is found first. Set
 * EnglishFontFile / JapaneseFontFile to use other files. Without them it falls back to the engine fonts.
 *
 * AFootballDrillManager installs this on the player when the game starts, so there is nothing to set
 * up in the editor. Turn off bInstallHud on the drill manager to keep the project's own HUD instead.
 */
UCLASS()
class FOOTBALL_API AFootballHUD : public AHUD
{
	GENERATED_BODY()

public:
	/** Colours, so they are easy to change in one place. */
	UPROPERTY(EditDefaultsOnly, Category = "Football HUD")
	FLinearColor CrosshairColor = FLinearColor(1.f, 1.f, 1.f, 0.85f);

	UPROPERTY(EditDefaultsOnly, Category = "Football HUD")
	FLinearColor AimMarkerColor = FLinearColor(0.35f, 0.95f, 1.f, 0.9f);

	/** Radius of the ring drawn on the ground where the ball is expected to stop, in cm. */
	UPROPERTY(EditDefaultsOnly, Category = "Football HUD", meta = (ClampMin = "5.0"))
	float AimMarkerRadius = 55.f;

	/** Size of all text, on top of the scaling with screen resolution. 1 is the original size. */
	UPROPERTY(EditDefaultsOnly, Category = "Football HUD", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float TextScale = 1.4f;

	/** Opens the "back to main menu?" question during play, and goes back a screen in the menus. */
	UPROPERTY(EditDefaultsOnly, Category = "Football HUD")
	FKey MenuKey = EKeys::Escape;

	/** Does the same. Useful in the editor, where Esc ends Play In Editor before the game sees it. */
	UPROPERTY(EditDefaultsOnly, Category = "Football HUD")
	FKey AltMenuKey = EKeys::Tab;

	/** Title on the main menu, in English and in Japanese. */
	UPROPERTY(EditDefaultsOnly, Category = "Football HUD")
	FString GameTitle = TEXT("ScanPass");

	UPROPERTY(EditDefaultsOnly, Category = "Football HUD")
	FString GameTitleJapanese = TEXT("スキャンパス");

	/**
	 * Font file (.ttf, .otf or .ttc) for Japanese text. Leave empty to use a Japanese font that comes
	 * with Windows.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Football HUD|Language")
	FString JapaneseFontFile;

	/** Font file for English text. Leave empty to use Segoe UI, which comes with Windows. */
	UPROPERTY(EditDefaultsOnly, Category = "Football HUD|Language")
	FString EnglishFontFile;

	/** Language used the very first time the game runs. After that the player's choice is remembered. */
	UPROPERTY(EditDefaultsOnly, Category = "Football HUD|Language")
	bool bDefaultToSystemLanguage = true;

	UFUNCTION(BlueprintCallable, Category = "Football HUD")
	void SetLanguage(EFootballHudLanguage NewLanguage);

	UFUNCTION(BlueprintPure, Category = "Football HUD")
	EFootballHudLanguage GetLanguage() const { return Language; }

	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

private:
	enum class EScreen : uint8
	{
		Playing,
		MainMenu,
		HowToPlay,
		ConfirmExit
	};

	/** Everything the HUD can say. Tr() gives the text in the current language. */
	enum class ETextId : uint8
	{
		Subtitle,
		StartGame,
		HowToPlay,
		ExitGame,
		NoDrill,
		Language,
		ConfirmQuestion,
		ConfirmDetail,
		MainMenu,
		KeepPlaying,
		HowToPlayTitle,
		KeysMove,
		LegendMove,
		KeysScan,
		LegendScan,
		KeysKick,
		LegendKick,
		KeysAim,
		LegendAim,
		KeysRestart,
		LegendRestart,
		KeysMenu,
		LegendMenu,
		NoteTeams,
		NoteLives,
		Back,
		MouseLabel,
		Lives,
		PowerFormat,
		HoldToCharge,
		MetersFormat,
		SecondsFormat,
		ScoreFormat,
		PlayHint,
		OutOfLives,
		PassesCompletedFormat,
		ReachedLevelFormat,
		RestartFormat,
		Stage1,
		Stage2,
		Stage3,
		Stage4,
		Stage5,
		EndlessFormat,
		MessageGo,
		MessageTooSlow,
		MessageIntercepted,
		MessageNobodyGotIt,
		MessagePassCompleted,
		OneLifeLeftFormat,
		LivesLeftFormat,
		Count
	};

	/** Text in the current language. Formats use {0}, {1}, ... and go through Format(). */
	const TCHAR* Tr(ETextId Id) const;
	FString Format(ETextId Id, const FStringFormatOrderedArguments& Arguments) const;
	FString GetStageName(int32 Level, bool bEndless) const;
	FString GetDrillMessage(EFootballDrillMessage Message, int32 LivesLeftToShow) const;

	EFootballHudLanguage Language = EFootballHudLanguage::English;

	/** Fonts with Japanese glyphs, the same sizes as the engine's small, medium and large fonts. Null if none was found. */
	UPROPERTY(Transient)
	TObjectPtr<UFont> JapaneseSmallFont;

	UPROPERTY(Transient)
	TObjectPtr<UFont> JapaneseMediumFont;

	UPROPERTY(Transient)
	TObjectPtr<UFont> JapaneseLargeFont;

	/** The same for English, from a system font, so it can be drawn sharp at any size. Null if none was found. */
	UPROPERTY(Transient)
	TObjectPtr<UFont> EnglishSmallFont;

	UPROPERTY(Transient)
	TObjectPtr<UFont> EnglishMediumFont;

	UPROPERTY(Transient)
	TObjectPtr<UFont> EnglishLargeFont;

	void LoadFonts();
	/** First file of the list that exists: the override (relative to the project), then the Windows fonts. */
	static FString FindFontFile(const FString& Override, const TArray<const TCHAR*>& SystemFiles);
	/** English font of the size of the given engine font. */
	UFont* EnglishFont(UFont* EngineFont, UFont* Loaded) const { return Loaded != nullptr ? Loaded : EngineFont; }
	UFont* MakeFontFromFile(const FString& FontFile, const UFont* SizeLike);
	bool HasJapaneseFont() const { return JapaneseMediumFont != nullptr; }

	/** Fonts for the current language. */
	UFont* SmallFont() const;
	UFont* MediumFont() const;
	UFont* LargeFont() const;

	/** Text scale: screen scale times TextScale times Size. */
	float TextSize(float Size = 1.f) const { return UiScale * TextScale * Size; }

	EScreen Screen = EScreen::Playing;
	bool bScreenChosen = false;

	/** Mouse state for this frame, read once at the top of DrawHUD. */
	float MouseX = 0.f;
	float MouseY = 0.f;
	bool bMouseValid = false;
	bool bMouseClicked = false;

	/** Switches screen, and with it the pause, the mouse cursor and the input mode. */
	void SetScreen(EScreen NewScreen);

	void DrawGameplay(const UBallKickComponent* KickComponent, const FFootballDrillHudState& DrillState);
	void DrawMainMenu(AFootballDrillManager* Drill);
	void DrawLanguageSwitch();
	void DrawHowToPlay();
	void DrawConfirmExit(AFootballDrillManager* Drill);
	void DrawKeyboardDiagram(float Left, float Top, float KeySize, float& OutWidth, float& OutHeight);
	void DrawMouseDiagram(float Left, float Top, float Width, float Height);

	/**
	 * Draws a button centred on CenterX and returns true if it was clicked this frame. bSelected draws it
	 * lit up, for the choice that is currently active. LabelFont overrides the font of the label.
	 */
	bool DrawButton(const FString& Label, float CenterX, float Y, float Width, float Height, bool bEnabled = true,
		bool bSelected = false, UFont* LabelFont = nullptr, float LabelSize = 1.2f);

	/** Canvas height / 1080, so the HUD keeps its proportions at any resolution. */
	float UiScale = 1.f;

	void DrawCrosshair();
	void DrawAimMarker(const UBallKickComponent* KickComponent);
	void DrawPowerBar(const UBallKickComponent* KickComponent);
	void DrawLives(const FFootballDrillHudState& DrillState);
	void DrawClock(const FFootballDrillHudState& DrillState);
	void DrawScore(const FFootballDrillHudState& DrillState);
	void DrawResultMessage(const FFootballDrillHudState& DrillState);
	void DrawGameOver(const FFootballDrillHudState& DrillState);

	/** Draws a bar with a border and a proportional fill. */
	void DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& FillColor, const FLinearColor& BackColor);
	/** Draws a rectangle outline out of four thin rectangles. */
	void DrawOutline(float X, float Y, float Width, float Height, float Thickness, const FLinearColor& Color);
	/** Centres text horizontally on the given screen X. Returns the height it took. */
	float DrawCenteredText(const FString& Text, const FLinearColor& Color, float CenterX, float Y, UFont* Font, float Scale);
	/** Draws text so that it ends at RightX. Returns the height it took. */
	float DrawRightAlignedText(const FString& Text, const FLinearColor& Color, float RightX, float Y, UFont* Font, float Scale);
	/** Height of one line of text in this font and scale. */
	float GetLineHeight(UFont* Font, float Scale);

	/**
	 * Text drawing and measuring that keeps text sharp at any size: the font is made at the size it is
	 * shown at, instead of the canvas stretching a small font. Use these instead of DrawText / GetTextSize.
	 */
	void DrawTextSharp(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale);
	void GetTextSizeSharp(const FString& Text, float& OutWidth, float& OutHeight, UFont* Font, float Scale);
	/** The same font at the size it will be shown at, and the scale left to apply (normally 1). */
	UFont* GetSharpFont(UFont* Font, float& InOutScale);

	/** Fonts made by GetSharpFont, by source font and pixel size. SharpFontRefs keeps them alive. */
	TMap<TPair<const UFont*, int32>, UFont*> SharpFonts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UFont>> SharpFontRefs;
};
