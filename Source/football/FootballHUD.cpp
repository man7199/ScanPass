#include "FootballHUD.h"

#include "BallKickComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Fonts/CompositeFont.h"
#include "FootballDrillManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FootballHUD)

namespace FootballHud
{
	/** Everything below is in pixels at 1080p and scaled by UiScale. */
	static const float Margin = 28.f;
	static const float CrosshairGap = 5.f;
	static const float CrosshairArm = 9.f;
	static const float CrosshairThickness = 2.f;

	static const float PowerBarWidth = 280.f;
	static const float PowerBarHeight = 18.f;
	static const float PowerBarBottom = 70.f;

	static const float LifeSize = 30.f;
	static const float LifeGap = 10.f;

	static const float ClockBarWidth = 200.f;
	static const float ClockBarHeight = 10.f;

	static const int32 AimRingSegments = 20;

	static const FLinearColor PanelColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);
	static const FLinearColor BarBackColor = FLinearColor(0.f, 0.f, 0.f, 0.45f);
	static const FLinearColor TextColor = FLinearColor(1.f, 1.f, 1.f, 0.9f);
	static const FLinearColor DimTextColor = FLinearColor(1.f, 1.f, 1.f, 0.6f);
	static const FLinearColor LifeColor = FLinearColor(1.f, 0.3f, 0.3f, 0.95f);
	static const FLinearColor LostLifeColor = FLinearColor(0.02f, 0.06f, 0.12f, 0.55f);

	/** Text in the top corners during play: dark, so it reads against the bright grass behind it. */
	static const FLinearColor CornerTextColor = FLinearColor(0.02f, 0.06f, 0.14f, 1.f);
	static const FLinearColor CornerDimTextColor = FLinearColor(0.02f, 0.06f, 0.14f, 0.8f);

	/** Where the chosen language is saved (GameUserSettings.ini). */
	static const TCHAR* ConfigSection = TEXT("FootballHUD");
	static const TCHAR* ConfigLanguageKey = TEXT("Language");

	/** Every line the HUD shows, in English and Japanese. Same order as AFootballHUD::ETextId. */
	struct FLine
	{
		const TCHAR* English;
		const TCHAR* Japanese;
	};

	static const FLine Lines[] =
	{
		/* Subtitle */				{ TEXT("A football training game focused on scanning and possession"), TEXT("スキャニングとポゼッションサッカーを鍛えるサッカートレーニングゲーム") },
		/* StartGame */				{ TEXT("START GAME"), TEXT("ゲーム開始") },
		/* HowToPlay */				{ TEXT("HOW TO PLAY"), TEXT("遊び方") },
		/* ExitGame */				{ TEXT("EXIT GAME"), TEXT("ゲーム終了") },
		/* NoDrill */				{ TEXT("No Football Drill Manager with a ball found in this level."), TEXT("このレベルにボール付きの Football Drill Manager が見つかりません。") },
		/* Language */				{ TEXT("LANGUAGE"), TEXT("言語") },
		/* ConfirmQuestion */		{ TEXT("Back to the main menu?"), TEXT("メインメニューに戻りますか？") },
		/* ConfirmDetail */			{ TEXT("This run will end."), TEXT("今回のプレイは終了します。") },
		/* MainMenu */				{ TEXT("MAIN MENU"), TEXT("メインメニュー") },
		/* KeepPlaying */			{ TEXT("KEEP PLAYING"), TEXT("プレイを続ける") },
		/* HowToPlayTitle */		{ TEXT("HOW TO PLAY"), TEXT("遊び方") },
		/* KeysMove */				{ TEXT("W A S D"), TEXT("W A S D") },
		/* LegendMove */			{ TEXT("Move. The ball stays at your feet while you have it."), TEXT("移動。ボールを持っている間は足元にキープされます。") },
		/* KeysScan */				{ TEXT("Q / E"), TEXT("Q / E") },
		/* LegendScan */			{ TEXT("Hold to glance left or right. A pass goes where you are looking."), TEXT("長押しで左右を確認（首振り）。パスは見ている方向に出ます。") },
		/* KeysKick */				{ TEXT("Left mouse"), TEXT("左クリック") },
		/* LegendKick */			{ TEXT("Hold to build power (half a second doubles it), release to pass."), TEXT("長押しでパワーをためて（0.5秒で2倍）、離すとパス。") },
		/* KeysAim */				{ TEXT("Mouse"), TEXT("マウス") },
		/* LegendAim */				{ TEXT("Aim. Look level for a ground pass, look up to lift it. The ring is where it stops."), TEXT("狙う。水平ならグラウンダー、上を向くと浮き球。リングが止まる位置です。") },
		/* KeysRestart */			{ TEXT("R"), TEXT("R") },
		/* LegendRestart */			{ TEXT("Try again when you are out of lives."), TEXT("ライフがなくなったらリトライ。") },
		/* KeysMenu */				{ TEXT("Esc / Tab"), TEXT("Esc / Tab") },
		/* LegendMenu */			{ TEXT("Back to the main menu."), TEXT("メインメニューに戻る。") },
		/* NoteTeams */				{ TEXT("Pass to a team-mate (blue marker). Opponents (red) cut out passes that come near them."), TEXT("味方（青）にパスを出そう。相手（赤）は近くを通るパスをカットします。") },
		/* NoteLives */				{ TEXT("An interception, a pass nobody reaches, or too long on the ball costs one of three lives."), TEXT("パスカット、誰も届かないパス、ボールの持ちすぎで、3つのライフを1つ失います。") },
		/* Back */					{ TEXT("BACK"), TEXT("戻る") },
		/* MouseLabel */			{ TEXT("MOUSE"), TEXT("マウス") },
		/* Lives */					{ TEXT("LIVES"), TEXT("ライフ") },
		/* PowerFormat */			{ TEXT("POWER  x{0}"), TEXT("パワー  x{0}") },
		/* HoldToCharge */			{ TEXT("Hold to charge"), TEXT("長押しでチャージ") },
		/* MetersFormat */			{ TEXT("{0} m"), TEXT("{0} m") },
		/* SecondsFormat */			{ TEXT("{0} s"), TEXT("{0} 秒") },
		/* ScoreFormat */			{ TEXT("LEVEL {0}   {1}      PASSES {2}"), TEXT("レベル {0}   {1}      パス成功 {2}") },
		/* PlayHint */				{ TEXT("Hold left click to pass   |   Q / E to scan"), TEXT("左クリック長押しでパス   |   Q / E で首振り") },
		/* OutOfLives */			{ TEXT("OUT OF LIVES"), TEXT("ライフがなくなった！") },
		/* PassesCompletedFormat */	{ TEXT("{0} passes completed"), TEXT("パス成功 {0} 本") },
		/* ReachedLevelFormat */	{ TEXT("reached level {0}, {1}"), TEXT("到達レベル {0}（{1}）") },
		/* RestartFormat */			{ TEXT("{0}: try again      Esc: main menu"), TEXT("{0}：リトライ      Esc：メインメニュー") },
		/* Stage1 */				{ TEXT("Beginner"), TEXT("初級") },
		/* Stage2 */				{ TEXT("Amateur"), TEXT("アマチュア") },
		/* Stage3 */				{ TEXT("Semi-Pro"), TEXT("セミプロ") },
		/* Stage4 */				{ TEXT("Professional"), TEXT("プロ") },
		/* Stage5 */				{ TEXT("Championship"), TEXT("チャンピオンシップ") },
		/* EndlessFormat */			{ TEXT("{0} (endless)"), TEXT("{0}（エンドレス）") },
		/* MessageGo */				{ TEXT("Go!"), TEXT("スタート！") },
		/* MessageTooSlow */		{ TEXT("Too slow!"), TEXT("遅い！") },
		/* MessageIntercepted */	{ TEXT("Intercepted!"), TEXT("パスカットされた！") },
		/* MessageNobodyGotIt */	{ TEXT("Nobody got it."), TEXT("誰も届かなかった…") },
		/* MessagePassCompleted */	{ TEXT("Pass completed!"), TEXT("パス成功！") },
		/* OneLifeLeftFormat */		{ TEXT("{0}   (1 life left)"), TEXT("{0}（残りライフ 1）") },
		/* LivesLeftFormat */		{ TEXT("{0}   ({1} lives left)"), TEXT("{0}（残りライフ {1}）") },
	};
}

// ---------------------------------------------------------------------------------------------------
// Language

const TCHAR* AFootballHUD::Tr(ETextId Id) const
{
	static_assert(UE_ARRAY_COUNT(FootballHud::Lines) == (int32)ETextId::Count, "One line of text per ETextId, in the same order.");

	const FootballHud::FLine& Line = FootballHud::Lines[(int32)Id];
	return (Language == EFootballHudLanguage::Japanese) ? Line.Japanese : Line.English;
}

FString AFootballHUD::Format(ETextId Id, const FStringFormatOrderedArguments& Arguments) const
{
	return FString::Format(Tr(Id), Arguments);
}

FString AFootballHUD::GetStageName(int32 Level, bool bEndless) const
{
	// Level counts from 0. Anything past the fifth is the endless level.
	static const ETextId Stages[] = { ETextId::Stage1, ETextId::Stage2, ETextId::Stage3, ETextId::Stage4, ETextId::Stage5 };
	const FString Name = Tr(Stages[FMath::Clamp(Level, 0, (int32)UE_ARRAY_COUNT(Stages) - 1)]);
	return bEndless ? Format(ETextId::EndlessFormat, { Name }) : Name;
}

FString AFootballHUD::GetDrillMessage(EFootballDrillMessage Message, int32 LivesLeftToShow) const
{
	FString Text;
	switch (Message)
	{
	case EFootballDrillMessage::Go: Text = Tr(ETextId::MessageGo); break;
	case EFootballDrillMessage::TooSlow: Text = Tr(ETextId::MessageTooSlow); break;
	case EFootballDrillMessage::Intercepted: Text = Tr(ETextId::MessageIntercepted); break;
	case EFootballDrillMessage::NobodyGotIt: Text = Tr(ETextId::MessageNobodyGotIt); break;
	case EFootballDrillMessage::PassCompleted: Text = Tr(ETextId::MessagePassCompleted); break;
	default: return FString();
	}

	if (LivesLeftToShow == 1)
	{
		return Format(ETextId::OneLifeLeftFormat, { Text });
	}
	if (LivesLeftToShow > 1)
	{
		return Format(ETextId::LivesLeftFormat, { Text, LivesLeftToShow });
	}
	return Text;
}

void AFootballHUD::BeginPlay()
{
	Super::BeginPlay();

	LoadFonts();

	// The player's last choice, or the system language the first time.
	FString Saved;
	if (GConfig != nullptr && GConfig->GetString(FootballHud::ConfigSection, FootballHud::ConfigLanguageKey, Saved, GGameUserSettingsIni))
	{
		Language = Saved.Equals(TEXT("Japanese"), ESearchCase::IgnoreCase) ? EFootballHudLanguage::Japanese : EFootballHudLanguage::English;
	}
	else if (bDefaultToSystemLanguage)
	{
		const FString SystemLanguage = FInternationalization::Get().GetCurrentCulture()->GetTwoLetterISOLanguageName();
		Language = SystemLanguage.Equals(TEXT("ja"), ESearchCase::IgnoreCase) ? EFootballHudLanguage::Japanese : EFootballHudLanguage::English;
	}
}

void AFootballHUD::SetLanguage(EFootballHudLanguage NewLanguage)
{
	Language = NewLanguage;

	if (GConfig != nullptr)
	{
		GConfig->SetString(FootballHud::ConfigSection, FootballHud::ConfigLanguageKey,
			NewLanguage == EFootballHudLanguage::Japanese ? TEXT("Japanese") : TEXT("English"), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

UFont* AFootballHUD::MakeFontFromFile(const FString& FontFile, const UFont* SizeLike)
{
	// A runtime font, rasterised on demand from the file, the same way the engine's own fonts work.
	UFont* Font = NewObject<UFont>(this);
	Font->FontCacheType = EFontCacheType::Runtime;
	Font->LegacyFontSize = (SizeLike != nullptr) ? SizeLike->LegacyFontSize : 20;

	FTypefaceEntry& Entry = Font->CompositeFont.DefaultTypeface.Fonts.AddDefaulted_GetRef();
	Entry.Name = TEXT("Regular");
	Entry.Font = FFontData(FontFile, EFontHinting::Default, EFontLoadingPolicy::Stream);
	Font->LegacyFontName = Entry.Name;
	return Font;
}

FString AFootballHUD::FindFontFile(const FString& Override, const TArray<const TCHAR*>& SystemFiles)
{
	TArray<FString> Candidates;
	if (!Override.IsEmpty())
	{
		Candidates.Add(FPaths::IsRelative(Override) ? FPaths::Combine(FPaths::ProjectDir(), Override) : Override);
	}

	FString WindowsDir = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
	if (WindowsDir.IsEmpty())
	{
		WindowsDir = TEXT("C:/Windows");
	}
	const FString FontsDir = FPaths::Combine(WindowsDir, TEXT("Fonts"));
	for (const TCHAR* File : SystemFiles)
	{
		Candidates.Add(FPaths::Combine(FontsDir, File));
	}

	for (const FString& Candidate : Candidates)
	{
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}
	return FString();
}

void AFootballHUD::LoadFonts()
{
	// Fonts from files, so that GetSharpFont can make them at any size. The engine's own fonts are
	// fixed-size bitmaps that go blurry when scaled up, and they have no Japanese glyphs at all.
	const FString JapaneseFile = FindFontFile(JapaneseFontFile,
		{ TEXT("YuGothB.ttc"), TEXT("YuGothM.ttc"), TEXT("meiryob.ttc"), TEXT("meiryo.ttc"), TEXT("msgothic.ttc") });
	if (!JapaneseFile.IsEmpty())
	{
		JapaneseSmallFont = MakeFontFromFile(JapaneseFile, UEngine::GetSmallFont());
		JapaneseMediumFont = MakeFontFromFile(JapaneseFile, UEngine::GetMediumFont());
		JapaneseLargeFont = MakeFontFromFile(JapaneseFile, UEngine::GetLargeFont());
	}

	const FString EnglishFile = FindFontFile(EnglishFontFile,
		{ TEXT("segoeuib.ttf"), TEXT("seguisb.ttf"), TEXT("segoeui.ttf"), TEXT("arialbd.ttf"), TEXT("arial.ttf") });
	if (!EnglishFile.IsEmpty())
	{
		EnglishSmallFont = MakeFontFromFile(EnglishFile, UEngine::GetSmallFont());
		EnglishMediumFont = MakeFontFromFile(EnglishFile, UEngine::GetMediumFont());
		EnglishLargeFont = MakeFontFromFile(EnglishFile, UEngine::GetLargeFont());
	}
}

UFont* AFootballHUD::SmallFont() const
{
	return (Language == EFootballHudLanguage::Japanese && JapaneseSmallFont != nullptr) ? JapaneseSmallFont.Get() : EnglishFont(UEngine::GetSmallFont(), EnglishSmallFont);
}

UFont* AFootballHUD::MediumFont() const
{
	return (Language == EFootballHudLanguage::Japanese && JapaneseMediumFont != nullptr) ? JapaneseMediumFont.Get() : EnglishFont(UEngine::GetMediumFont(), EnglishMediumFont);
}

UFont* AFootballHUD::LargeFont() const
{
	return (Language == EFootballHudLanguage::Japanese && JapaneseLargeFont != nullptr) ? JapaneseLargeFont.Get() : EnglishFont(UEngine::GetLargeFont(), EnglishLargeFont);
}

// ---------------------------------------------------------------------------------------------------
// Screens

void AFootballHUD::SetScreen(EScreen NewScreen)
{
	Screen = NewScreen;
	const bool bMenuOpen = NewScreen != EScreen::Playing;

	APlayerController* PlayerController = GetOwningPlayerController();
	if (PlayerController == nullptr)
	{
		return;
	}

	// A menu needs a free mouse cursor and a frozen game; play needs the opposite.
	PlayerController->bShowMouseCursor = bMenuOpen;
	if (bMenuOpen)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PlayerController->SetInputMode(InputMode);
	}
	else
	{
		PlayerController->SetInputMode(FInputModeGameOnly());

		// The click that closed the menu must not turn into a kick.
		if (const APawn* Pawn = PlayerController->GetPawn())
		{
			if (UBallKickComponent* KickComponent = Pawn->FindComponentByClass<UBallKickComponent>())
			{
				KickComponent->IgnoreKeyUntilReleased();
			}
		}
	}

	UGameplayStatics::SetGamePaused(this, bMenuOpen);
}

void AFootballHUD::DrawHUD()
{
	Super::DrawHUD();

	if (Canvas == nullptr)
	{
		return;
	}

	UiScale = FMath::Max((float)Canvas->SizeY / 1080.f, 0.5f);

	APlayerController* PlayerController = GetOwningPlayerController();
	AFootballDrillManager* Drill = AFootballDrillManager::Get(GetWorld());

	// Decide once, when the drill is ready, whether the game opens on the main menu.
	if (!bScreenChosen && Drill != nullptr && Drill->IsReady())
	{
		bScreenChosen = true;
		SetScreen(Drill->IsWaitingInMenu() ? EScreen::MainMenu : EScreen::Playing);
	}

	// Input for this frame. DrawHUD keeps running while the game is paused, which is what lets the
	// menus work without any widgets.
	bMouseValid = false;
	bMouseClicked = false;
	bool bMenuKeyPressed = false;
	if (PlayerController != nullptr)
	{
		bMouseValid = PlayerController->GetMousePosition(MouseX, MouseY);
		bMouseClicked = PlayerController->WasInputKeyJustPressed(EKeys::LeftMouseButton);
		bMenuKeyPressed = PlayerController->WasInputKeyJustPressed(MenuKey) || PlayerController->WasInputKeyJustPressed(AltMenuKey);
	}

	const UBallKickComponent* KickComponent = nullptr;
	if (PlayerController != nullptr)
	{
		if (const APawn* Pawn = PlayerController->GetPawn())
		{
			KickComponent = Pawn->FindComponentByClass<UBallKickComponent>();
		}
	}

	// Without a drill in the level only the crosshair and the power bar are drawn.
	static const FFootballDrillHudState NoDrill;
	const FFootballDrillHudState& DrillState = (Drill != nullptr) ? Drill->GetHudState() : NoDrill;

	switch (Screen)
	{
	case EScreen::Playing:
		DrawGameplay(KickComponent, DrillState);
		if (bMenuKeyPressed && Drill != nullptr && Drill->IsReady())
		{
			SetScreen(EScreen::ConfirmExit);
		}
		break;

	case EScreen::ConfirmExit:
		DrawGameplay(nullptr, DrillState);
		DrawConfirmExit(Drill);
		if (bMenuKeyPressed && Screen == EScreen::ConfirmExit)
		{
			SetScreen(EScreen::Playing);	// the same key closes the question again
		}
		break;

	case EScreen::MainMenu:
		DrawMainMenu(Drill);
		break;

	case EScreen::HowToPlay:
		DrawHowToPlay();
		if (bMenuKeyPressed && Screen == EScreen::HowToPlay)
		{
			SetScreen(EScreen::MainMenu);
		}
		break;
	}
}

void AFootballHUD::DrawGameplay(const UBallKickComponent* KickComponent, const FFootballDrillHudState& DrillState)
{
	const bool bGameOver = DrillState.bActive && DrillState.bGameOver;

	if (!bGameOver)
	{
		DrawCrosshair();
		if (KickComponent != nullptr)
		{
			DrawAimMarker(KickComponent);
			DrawPowerBar(KickComponent);
		}
	}

	if (DrillState.bActive)
	{
		DrawScore(DrillState);
		DrawLives(DrillState);

		if (!bGameOver)
		{
			DrawClock(DrillState);
		}

		DrawResultMessage(DrillState);

		if (bGameOver)
		{
			DrawGameOver(DrillState);
		}
	}
}

bool AFootballHUD::DrawButton(const FString& Label, float CenterX, float Y, float Width, float Height, bool bEnabled, bool bSelected, UFont* LabelFont, float LabelSize)
{
	const float X = CenterX - Width * 0.5f;
	const bool bHovered = bEnabled && bMouseValid && MouseX >= X && MouseX <= X + Width && MouseY >= Y && MouseY <= Y + Height;

	const FLinearColor BackColor = !bEnabled ? FLinearColor(0.f, 0.f, 0.f, 0.3f)
		: bHovered ? FLinearColor(0.15f, 0.55f, 0.25f, 0.95f)
		: bSelected ? FLinearColor(0.1f, 0.4f, 0.18f, 0.95f)
		: FLinearColor(0.02f, 0.06f, 0.03f, 0.8f);
	const FLinearColor EdgeColor = (bHovered || bSelected) ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.45f);
	const FLinearColor LabelColor = !bEnabled ? FLinearColor(1.f, 1.f, 1.f, 0.35f)
		: (bSelected || bHovered) ? FLinearColor::White
		: FLinearColor(1.f, 1.f, 1.f, 0.75f);

	DrawRect(BackColor, X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, FMath::Max(1.f, (bSelected ? 3.f : 2.f) * UiScale), EdgeColor);

	UFont* Font = (LabelFont != nullptr) ? LabelFont : LargeFont();
	const float Scale = TextSize(LabelSize);
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSizeSharp(Label, TextWidth, TextHeight, Font, Scale);
	DrawTextSharp(Label, LabelColor, CenterX - TextWidth * 0.5f, Y + (Height - TextHeight) * 0.5f, Font, Scale);

	return bHovered && bMouseClicked;
}

void AFootballHUD::DrawMainMenu(AFootballDrillManager* Drill)
{
	const float SizeX = (float)Canvas->SizeX;
	const float SizeY = (float)Canvas->SizeY;
	const float CenterX = SizeX * 0.5f;

	// The paused pitch stays visible behind a dark veil.
	DrawRect(FLinearColor(0.f, 0.02f, 0.f, 0.72f), 0.f, 0.f, SizeX, SizeY);

	// Laid out upwards from the buttons, so the tagline always sits between the title and the buttons,
	// whatever size the fonts turn out to be.
	const float ButtonsTop = SizeY * 0.46f;

	const FString Tagline = Tr(ETextId::Subtitle);
	float TaglineScale = TextSize(1.1f);
	float TaglineWidth = 0.f;
	float TaglineHeight = 0.f;
	GetTextSizeSharp(Tagline, TaglineWidth, TaglineHeight, MediumFont(), TaglineScale);
	if (TaglineWidth > SizeX * 0.9f && TaglineWidth > 0.f)
	{
		// Too wide for the screen: shrink it to fit.
		TaglineScale *= SizeX * 0.9f / TaglineWidth;
		GetTextSizeSharp(Tagline, TaglineWidth, TaglineHeight, MediumFont(), TaglineScale);
	}
	TaglineHeight = FMath::Max(TaglineHeight, 20.f * UiScale);
	const float TaglineY = ButtonsTop - 48.f * UiScale - TaglineHeight;

	const FString& Title = (Language == EFootballHudLanguage::Japanese && HasJapaneseFont()) ? GameTitleJapanese : GameTitle;
	const float TitleScale = TextSize(2.6f);
	const float TitleHeight = FMath::Max(GetLineHeight(LargeFont(), TitleScale), 40.f * UiScale);
	const float TitleY = FMath::Max(TaglineY - 12.f * UiScale - TitleHeight, FootballHud::Margin * UiScale);

	DrawCenteredText(Title, FLinearColor::White, CenterX, TitleY, LargeFont(), TitleScale);
	DrawCenteredText(Tagline, FLinearColor(0.75f, 0.95f, 0.8f, 0.95f), CenterX, TaglineY, MediumFont(), TaglineScale);

	const float ButtonWidth = 300.f * UiScale * TextScale;
	const float ButtonHeight = 56.f * UiScale * TextScale;
	const float ButtonGap = 22.f * UiScale;
	float Y = ButtonsTop;

	const bool bCanStart = Drill != nullptr && Drill->IsReady();

	if (DrawButton(Tr(ETextId::StartGame), CenterX, Y, ButtonWidth, ButtonHeight, bCanStart))
	{
		Drill->StartFromMenu();
		SetScreen(EScreen::Playing);
		return;
	}
	Y += ButtonHeight + ButtonGap;

	if (DrawButton(Tr(ETextId::HowToPlay), CenterX, Y, ButtonWidth, ButtonHeight))
	{
		SetScreen(EScreen::HowToPlay);
		return;
	}
	Y += ButtonHeight + ButtonGap;

	if (DrawButton(Tr(ETextId::ExitGame), CenterX, Y, ButtonWidth, ButtonHeight))
	{
		UKismetSystemLibrary::QuitGame(this, GetOwningPlayerController(), EQuitPreference::Quit, false);
		return;
	}

	if (!bCanStart)
	{
		DrawCenteredText(Tr(ETextId::NoDrill), FLinearColor(1.f, 0.5f, 0.4f, 1.f),
			CenterX, Y + ButtonHeight + 24.f * UiScale, MediumFont(), TextSize());
	}

	DrawLanguageSwitch();
}

void AFootballHUD::DrawLanguageSwitch()
{
	// Top right corner: a label and the two languages side by side, each written in its own language.
	const float Right = (float)Canvas->SizeX - FootballHud::Margin * UiScale;
	const float Top = FootballHud::Margin * UiScale;
	const float ButtonWidth = 118.f * UiScale * TextScale;
	const float ButtonHeight = 40.f * UiScale * TextScale;
	const float Gap = 10.f * UiScale;

	const float LabelHeight = DrawRightAlignedText(Tr(ETextId::Language), FootballHud::DimTextColor, Right, Top, SmallFont(), TextSize());
	const float ButtonY = Top + LabelHeight + 6.f * UiScale;

	const float JapaneseCenter = Right - ButtonWidth * 0.5f;
	const float EnglishCenter = JapaneseCenter - ButtonWidth - Gap;
	const bool bJapanese = Language == EFootballHudLanguage::Japanese;

	if (DrawButton(TEXT("English"), EnglishCenter, ButtonY, ButtonWidth, ButtonHeight, true, !bJapanese, EnglishFont(UEngine::GetMediumFont(), EnglishMediumFont), 1.f))
	{
		SetLanguage(EFootballHudLanguage::English);
	}

	// Without a Japanese font the option would only show empty boxes.
	UFont* JapaneseLabelFont = HasJapaneseFont() ? JapaneseMediumFont.Get() : EnglishFont(UEngine::GetMediumFont(), EnglishMediumFont);
	if (DrawButton(TEXT("日本語"), JapaneseCenter, ButtonY, ButtonWidth, ButtonHeight, HasJapaneseFont(), bJapanese, JapaneseLabelFont, 1.f))
	{
		SetLanguage(EFootballHudLanguage::Japanese);
	}
}

void AFootballHUD::DrawConfirmExit(AFootballDrillManager* Drill)
{
	const float SizeX = (float)Canvas->SizeX;
	const float SizeY = (float)Canvas->SizeY;
	const float CenterX = SizeX * 0.5f;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), 0.f, 0.f, SizeX, SizeY);

	const float PanelWidth = 560.f * UiScale * TextScale;
	const float PanelHeight = 250.f * UiScale * TextScale;
	const float PanelX = CenterX - PanelWidth * 0.5f;
	const float PanelY = SizeY * 0.5f - PanelHeight * 0.5f;
	DrawRect(FLinearColor(0.02f, 0.05f, 0.03f, 0.95f), PanelX, PanelY, PanelWidth, PanelHeight);
	DrawOutline(PanelX, PanelY, PanelWidth, PanelHeight, FMath::Max(1.f, 2.f * UiScale), FLinearColor(1.f, 1.f, 1.f, 0.5f));

	float Y = PanelY + 30.f * UiScale;
	Y += DrawCenteredText(Tr(ETextId::ConfirmQuestion), FLinearColor::White, CenterX, Y, LargeFont(), TextSize(1.5f));
	Y += 10.f * UiScale;
	DrawCenteredText(Tr(ETextId::ConfirmDetail), FootballHud::DimTextColor, CenterX, Y, MediumFont(), TextSize());

	const float ButtonWidth = 230.f * UiScale * TextScale;
	const float ButtonHeight = 52.f * UiScale * TextScale;
	const float ButtonY = PanelY + PanelHeight - ButtonHeight - 30.f * UiScale;
	const float Offset = ButtonWidth * 0.5f + 16.f * UiScale;

	if (DrawButton(Tr(ETextId::MainMenu), CenterX - Offset, ButtonY, ButtonWidth, ButtonHeight))
	{
		if (Drill != nullptr)
		{
			Drill->ReturnToMenu();
		}
		SetScreen(EScreen::MainMenu);
		return;
	}

	if (DrawButton(Tr(ETextId::KeepPlaying), CenterX + Offset, ButtonY, ButtonWidth, ButtonHeight))
	{
		SetScreen(EScreen::Playing);
	}
}

namespace FootballHud
{
	/** What a highlighted key on the diagram is for. */
	enum class EKeyUse : uint8
	{
		None,
		Move,
		Scan,
		Restart,
		Menu,
		Jump
	};

	struct FDiagramKey
	{
		const TCHAR* Label;
		float Width;	// in key widths
		EKeyUse Use;
	};

	static FLinearColor GetKeyUseColor(EKeyUse Use)
	{
		switch (Use)
		{
		case EKeyUse::Move: return FLinearColor(0.25f, 0.8f, 0.35f, 0.95f);
		case EKeyUse::Scan: return FLinearColor(0.2f, 0.7f, 0.95f, 0.95f);
		case EKeyUse::Restart: return FLinearColor(0.95f, 0.75f, 0.2f, 0.95f);
		case EKeyUse::Menu: return FLinearColor(0.9f, 0.35f, 0.3f, 0.95f);
		case EKeyUse::Jump: return FLinearColor(0.65f, 0.65f, 0.7f, 0.95f);
		default: return FLinearColor(1.f, 1.f, 1.f, 0.07f);
		}
	}

	static const FLinearColor KickColor = FLinearColor(0.95f, 0.5f, 0.2f, 0.95f);

	// The left-hand part of a keyboard, which is all this game uses.
	static const FDiagramKey KeyRow0[] = { { TEXT("Esc"), 1.f, EKeyUse::Menu }, { TEXT("1"), 1.f, EKeyUse::None }, { TEXT("2"), 1.f, EKeyUse::None }, { TEXT("3"), 1.f, EKeyUse::None }, { TEXT("4"), 1.f, EKeyUse::None }, { TEXT("5"), 1.f, EKeyUse::None }, { TEXT("6"), 1.f, EKeyUse::None }, { TEXT("7"), 1.f, EKeyUse::None } };
	static const FDiagramKey KeyRow1[] = { { TEXT("Tab"), 1.5f, EKeyUse::Menu }, { TEXT("Q"), 1.f, EKeyUse::Scan }, { TEXT("W"), 1.f, EKeyUse::Move }, { TEXT("E"), 1.f, EKeyUse::Scan }, { TEXT("R"), 1.f, EKeyUse::Restart }, { TEXT("T"), 1.f, EKeyUse::None }, { TEXT("Y"), 1.f, EKeyUse::None }, { TEXT("U"), 1.f, EKeyUse::None } };
	static const FDiagramKey KeyRow2[] = { { TEXT("Caps"), 1.8f, EKeyUse::None }, { TEXT("A"), 1.f, EKeyUse::Move }, { TEXT("S"), 1.f, EKeyUse::Move }, { TEXT("D"), 1.f, EKeyUse::Move }, { TEXT("F"), 1.f, EKeyUse::None }, { TEXT("G"), 1.f, EKeyUse::None }, { TEXT("H"), 1.f, EKeyUse::None }, { TEXT("J"), 1.f, EKeyUse::None } };
	static const FDiagramKey KeyRow3[] = { { TEXT("Shift"), 2.3f, EKeyUse::None }, { TEXT("Z"), 1.f, EKeyUse::None }, { TEXT("X"), 1.f, EKeyUse::None }, { TEXT("C"), 1.f, EKeyUse::None }, { TEXT("V"), 1.f, EKeyUse::None }, { TEXT("B"), 1.f, EKeyUse::None }, { TEXT("N"), 1.f, EKeyUse::None } };
	static const FDiagramKey KeyRow4[] = { { TEXT("Ctrl"), 1.5f, EKeyUse::None }, { TEXT("Alt"), 1.3f, EKeyUse::None }, { TEXT("Space"), 5.6f, EKeyUse::Jump } };
}

void AFootballHUD::DrawKeyboardDiagram(float Left, float Top, float KeySize, float& OutWidth, float& OutHeight)
{
	const float Gap = KeySize * 0.14f;
	const float Edge = FMath::Max(1.f, UiScale);
	OutWidth = 0.f;

	const TArrayView<const FootballHud::FDiagramKey> Rows[] =
	{
		MakeArrayView(FootballHud::KeyRow0),
		MakeArrayView(FootballHud::KeyRow1),
		MakeArrayView(FootballHud::KeyRow2),
		MakeArrayView(FootballHud::KeyRow3),
		MakeArrayView(FootballHud::KeyRow4),
	};

	// Key caps stay in the engine font: they are the same on a Japanese keyboard.
	UFont* KeyFont = EnglishFont(UEngine::GetSmallFont(), EnglishSmallFont);
	const float KeyTextScale = TextSize(0.9f);

	float Y = Top;
	for (const TArrayView<const FootballHud::FDiagramKey>& Row : Rows)
	{
		float X = Left;
		for (const FootballHud::FDiagramKey& Key : Row)
		{
			const float Width = KeySize * Key.Width + Gap * (Key.Width - 1.f);
			const bool bUsed = Key.Use != FootballHud::EKeyUse::None;

			DrawRect(FootballHud::GetKeyUseColor(Key.Use), X, Y, Width, KeySize);
			DrawOutline(X, Y, Width, KeySize, Edge, FLinearColor(1.f, 1.f, 1.f, bUsed ? 0.9f : 0.3f));

			const FString Label(Key.Label);
			float TextWidth = 0.f;
			float TextHeight = 0.f;
			GetTextSizeSharp(Label, TextWidth, TextHeight, KeyFont, KeyTextScale);
			DrawTextSharp(Label, bUsed ? FLinearColor::Black : FLinearColor(1.f, 1.f, 1.f, 0.45f),
				X + (Width - TextWidth) * 0.5f, Y + (KeySize - TextHeight) * 0.5f, KeyFont, KeyTextScale);

			X += Width + Gap;
		}
		OutWidth = FMath::Max(OutWidth, X - Gap - Left);
		Y += KeySize + Gap;
	}

	OutHeight = Y - Gap - Top;
}

void AFootballHUD::DrawMouseDiagram(float Left, float Top, float Width, float Height)
{
	const float Edge = FMath::Max(1.f, 2.f * UiScale);
	const float ButtonHeight = Height * 0.4f;
	const float HalfWidth = Width * 0.5f;

	// Body, then the two buttons on top of it. The left one is the kick.
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.07f), Left, Top, Width, Height);
	DrawRect(FootballHud::KickColor, Left, Top, HalfWidth, ButtonHeight);
	DrawOutline(Left, Top, Width, Height, Edge, FLinearColor(1.f, 1.f, 1.f, 0.8f));
	DrawOutline(Left, Top, HalfWidth, ButtonHeight, Edge, FLinearColor(1.f, 1.f, 1.f, 0.8f));
	DrawOutline(Left + HalfWidth, Top, HalfWidth, ButtonHeight, Edge, FLinearColor(1.f, 1.f, 1.f, 0.8f));

	DrawCenteredText(Tr(ETextId::MouseLabel), FootballHud::DimTextColor, Left + HalfWidth, Top + Height * 0.6f, SmallFont(), TextSize());
}

void AFootballHUD::DrawHowToPlay()
{
	const float SizeX = (float)Canvas->SizeX;
	const float SizeY = (float)Canvas->SizeY;
	const float CenterX = SizeX * 0.5f;

	DrawRect(FLinearColor(0.f, 0.02f, 0.f, 0.85f), 0.f, 0.f, SizeX, SizeY);
	float Y = SizeY * 0.04f;
	Y += DrawCenteredText(Tr(ETextId::HowToPlayTitle), FLinearColor::White, CenterX, Y, LargeFont(), TextSize(1.8f));
	Y += 24.f * UiScale;

	// Keyboard on the left, mouse on the right, laid out as one block and centred.
	const float KeySize = 50.f * UiScale;
	const float KeyboardWidthGuess = KeySize * 9.4f;
	const float MouseWidth = 120.f * UiScale;
	const float MouseHeight = 190.f * UiScale;
	const float BlockGap = 70.f * UiScale;
	const float BlockLeft = CenterX - (KeyboardWidthGuess + BlockGap + MouseWidth) * 0.5f;
	const float BlockTop = Y;

	float KeyboardWidth = 0.f;
	float KeyboardHeight = 0.f;
	DrawKeyboardDiagram(BlockLeft, BlockTop, KeySize, KeyboardWidth, KeyboardHeight);
	DrawMouseDiagram(BlockLeft + KeyboardWidth + BlockGap, BlockTop + (KeyboardHeight - MouseHeight) * 0.5f, MouseWidth, MouseHeight);

	// What each colour means.
	struct FLegendLine
	{
		FLinearColor Color;
		ETextId Keys;
		ETextId Text;
	};
	const FLegendLine Legend[] =
	{
		{ FootballHud::GetKeyUseColor(FootballHud::EKeyUse::Move), ETextId::KeysMove, ETextId::LegendMove },
		{ FootballHud::GetKeyUseColor(FootballHud::EKeyUse::Scan), ETextId::KeysScan, ETextId::LegendScan },
		{ FootballHud::KickColor, ETextId::KeysKick, ETextId::LegendKick },
		{ FLinearColor(1.f, 1.f, 1.f, 0.6f), ETextId::KeysAim, ETextId::LegendAim },
		{ FootballHud::GetKeyUseColor(FootballHud::EKeyUse::Restart), ETextId::KeysRestart, ETextId::LegendRestart },
		{ FootballHud::GetKeyUseColor(FootballHud::EKeyUse::Menu), ETextId::KeysMenu, ETextId::LegendMenu },
	};

	UFont* Font = MediumFont();
	const float LegendScale = TextSize(0.95f);
	const float NoteScale = TextSize(0.85f);
	const float LineHeight = GetLineHeight(Font, LegendScale) + 8.f * UiScale;
	const float Swatch = LineHeight * 0.45f;
	const float KeysColumn = Swatch + 14.f * UiScale;

	// The keys column is as wide as its widest entry, and the whole legend is centred on screen.
	float KeysWidth = 0.f;
	float TextWidth = 0.f;
	for (const FLegendLine& Line : Legend)
	{
		float Width = 0.f;
		float Height = 0.f;
		GetTextSizeSharp(Tr(Line.Keys), Width, Height, Font, LegendScale);
		KeysWidth = FMath::Max(KeysWidth, Width);
		GetTextSizeSharp(Tr(Line.Text), Width, Height, Font, LegendScale);
		TextWidth = FMath::Max(TextWidth, Width);
	}
	const float TextColumn = KeysColumn + KeysWidth + 24.f * UiScale;
	const float LegendLeft = FMath::Max(CenterX - (TextColumn + TextWidth) * 0.5f, FootballHud::Margin * UiScale);

	Y = BlockTop + KeyboardHeight + 30.f * UiScale;
	for (const FLegendLine& Line : Legend)
	{
		DrawRect(Line.Color, LegendLeft, Y + (LineHeight - Swatch) * 0.4f, Swatch, Swatch);
		DrawTextSharp(Tr(Line.Keys), FLinearColor::White, LegendLeft + KeysColumn, Y, Font, LegendScale);
		DrawTextSharp(Tr(Line.Text), FootballHud::TextColor, LegendLeft + TextColumn, Y, Font, LegendScale);
		Y += LineHeight;
	}

	Y += 12.f * UiScale;
	Y += DrawCenteredText(Tr(ETextId::NoteTeams), FootballHud::DimTextColor, CenterX, Y, Font, NoteScale) + 6.f * UiScale;
	Y += DrawCenteredText(Tr(ETextId::NoteLives), FootballHud::DimTextColor, CenterX, Y, Font, NoteScale);

	const float ButtonHeight = 50.f * UiScale * TextScale;
	const float ButtonY = FMath::Min(FMath::Max(Y + 24.f * UiScale, SizeY * 0.86f), SizeY - ButtonHeight - 12.f * UiScale);
	if (DrawButton(Tr(ETextId::Back), CenterX, ButtonY, 220.f * UiScale * TextScale, ButtonHeight))
	{
		SetScreen(EScreen::MainMenu);
	}
}

// ---------------------------------------------------------------------------------------------------
// Drawing helpers

void AFootballHUD::DrawOutline(float X, float Y, float Width, float Height, float Thickness, const FLinearColor& Color)
{
	DrawRect(Color, X, Y, Width, Thickness);
	DrawRect(Color, X, Y + Height - Thickness, Width, Thickness);
	DrawRect(Color, X, Y, Thickness, Height);
	DrawRect(Color, X + Width - Thickness, Y, Thickness, Height);
}

void AFootballHUD::DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& FillColor, const FLinearColor& BackColor)
{
	DrawRect(BackColor, X, Y, Width, Height);
	const float FillWidth = Width * FMath::Clamp(Fraction, 0.f, 1.f);
	if (FillWidth > 0.f)
	{
		DrawRect(FillColor, X, Y, FillWidth, Height);
	}
	DrawOutline(X, Y, Width, Height, FMath::Max(1.f, UiScale), FLinearColor(1.f, 1.f, 1.f, 0.5f));
}

float AFootballHUD::DrawCenteredText(const FString& Text, const FLinearColor& Color, float CenterX, float Y, UFont* Font, float Scale)
{
	float Width = 0.f;
	float Height = 0.f;
	GetTextSizeSharp(Text, Width, Height, Font, Scale);
	DrawTextSharp(Text, Color, CenterX - Width * 0.5f, Y, Font, Scale);
	return Height;
}

float AFootballHUD::DrawRightAlignedText(const FString& Text, const FLinearColor& Color, float RightX, float Y, UFont* Font, float Scale)
{
	float Width = 0.f;
	float Height = 0.f;
	GetTextSizeSharp(Text, Width, Height, Font, Scale);
	DrawTextSharp(Text, Color, RightX - Width, Y, Font, Scale);
	return Height;
}

float AFootballHUD::GetLineHeight(UFont* Font, float Scale)
{
	float Width = 0.f;
	float Height = 0.f;
	GetTextSizeSharp(TEXT("Ag"), Width, Height, Font, Scale);
	return Height;
}

UFont* AFootballHUD::GetSharpFont(UFont* Font, float& InOutScale)
{
	// Canvas text is drawn at the font's own size and then stretched, which turns big text blurry. A
	// runtime font can be made again at the size it is actually shown at, so it is drawn 1:1 instead.
	if (Font == nullptr || Font->FontCacheType != EFontCacheType::Runtime || Font->CompositeFont.DefaultTypeface.Fonts.Num() == 0)
	{
		return Font;
	}

	const int32 Size = FMath::Clamp(FMath::RoundToInt((float)Font->LegacyFontSize * InOutScale), 4, 400);
	if (Size == Font->LegacyFontSize)
	{
		InOutScale = 1.f;
		return Font;
	}

	const TPair<const UFont*, int32> Key(Font, Size);
	if (UFont* const* Found = SharpFonts.Find(Key))
	{
		InOutScale = 1.f;
		return *Found;
	}

	UFont* Sized = NewObject<UFont>(this);
	Sized->FontCacheType = EFontCacheType::Runtime;
	Sized->CompositeFont = Font->CompositeFont;
	Sized->LegacyFontName = Font->LegacyFontName;
	Sized->LegacyFontSize = Size;
	Sized->ScalingFactor = Font->ScalingFactor;

	SharpFontRefs.Add(Sized);
	SharpFonts.Add(Key, Sized);
	InOutScale = 1.f;
	return Sized;
}

void AFootballHUD::DrawTextSharp(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale)
{
	UFont* Sharp = GetSharpFont(Font, Scale);
	DrawText(Text, Color, FMath::RoundToFloat(X), FMath::RoundToFloat(Y), Sharp, Scale);
}

void AFootballHUD::GetTextSizeSharp(const FString& Text, float& OutWidth, float& OutHeight, UFont* Font, float Scale)
{
	UFont* Sharp = GetSharpFont(Font, Scale);
	GetTextSize(Text, OutWidth, OutHeight, Sharp, Scale);
}

// ---------------------------------------------------------------------------------------------------
// Gameplay HUD

void AFootballHUD::DrawCrosshair()
{
	// Plain cross with a gap in the middle: it shows where the camera points without hiding the ball.
	const float CenterX = (float)Canvas->SizeX * 0.5f;
	const float CenterY = (float)Canvas->SizeY * 0.5f;
	const float Gap = FootballHud::CrosshairGap * UiScale;
	const float Arm = FootballHud::CrosshairArm * UiScale;
	const float Thickness = FMath::Max(1.f, FootballHud::CrosshairThickness * UiScale);
	const float Half = Thickness * 0.5f;

	DrawRect(CrosshairColor, CenterX - Gap - Arm, CenterY - Half, Arm, Thickness);
	DrawRect(CrosshairColor, CenterX + Gap, CenterY - Half, Arm, Thickness);
	DrawRect(CrosshairColor, CenterX - Half, CenterY - Gap - Arm, Thickness, Arm);
	DrawRect(CrosshairColor, CenterX - Half, CenterY + Gap, Thickness, Arm);
	DrawRect(CrosshairColor, CenterX - Half, CenterY - Half, Thickness, Thickness);
}

void AFootballHUD::DrawAimMarker(const UBallKickComponent* KickComponent)
{
	// PredictKick fails when there is no ball in range, and a marker with no ball would be a lie.
	TArray<FVector> Path;
	FVector Target = FVector::ZeroVector;
	if (!KickComponent->PredictKick(KickComponent->GetPowerScale(), Path, Target) || Path.Num() < 2)
	{
		return;
	}

	const float Thickness = FMath::Max(1.f, 2.f * UiScale);

	// Dots along the predicted path, fading out towards the end.
	const float DotSize = FMath::Max(2.f, 3.f * UiScale);
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const FVector ScreenPoint = Canvas->Project(Path[Index], false);
		if (ScreenPoint.Z <= 0.0)
		{
			continue;
		}
		const float Fade = 1.f - 0.6f * ((float)Index / (float)Path.Num());
		const FLinearColor DotColor(AimMarkerColor.R, AimMarkerColor.G, AimMarkerColor.B, AimMarkerColor.A * 0.55f * Fade);
		DrawRect(DotColor, (float)ScreenPoint.X - DotSize * 0.5f, (float)ScreenPoint.Y - DotSize * 0.5f, DotSize, DotSize);
	}

	// Ring on the ground where the ball should come to rest. Built in world space and projected, so it
	// sits flat on the pitch and shrinks with distance like anything else.
	float PreviousX = 0.f;
	float PreviousY = 0.f;
	bool bHasPrevious = false;
	bool bRingComplete = true;

	for (int32 Segment = 0; Segment <= FootballHud::AimRingSegments; ++Segment)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * (double)Segment / (double)FootballHud::AimRingSegments;
		const FVector WorldPoint = Target + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * (double)AimMarkerRadius;
		const FVector ScreenPoint = Canvas->Project(WorldPoint, false);
		if (ScreenPoint.Z <= 0.0)
		{
			bHasPrevious = false;
			bRingComplete = false;
			continue;
		}

		const float PointX = (float)ScreenPoint.X;
		const float PointY = (float)ScreenPoint.Y;
		if (bHasPrevious)
		{
			DrawLine(PreviousX, PreviousY, PointX, PointY, AimMarkerColor, Thickness);
		}
		PreviousX = PointX;
		PreviousY = PointY;
		bHasPrevious = true;
	}

	// How far the pass would travel, next to the ring. Useful for judging the power bar.
	if (bRingComplete)
	{
		const FVector ScreenPoint = Canvas->Project(Target, false);
		if (ScreenPoint.Z > 0.0)
		{
			const double Meters = FVector::Dist2D(Path[0], Target) / 100.0;
			DrawCenteredText(Format(ETextId::MetersFormat, { FString::Printf(TEXT("%.0f"), Meters) }), AimMarkerColor,
				(float)ScreenPoint.X, (float)ScreenPoint.Y + 6.f * UiScale, SmallFont(), TextSize());
		}
	}
}

void AFootballHUD::DrawPowerBar(const UBallKickComponent* KickComponent)
{
	const float Width = FootballHud::PowerBarWidth * UiScale;
	const float Height = FootballHud::PowerBarHeight * UiScale;
	const float X = ((float)Canvas->SizeX - Width) * 0.5f;
	const float Y = (float)Canvas->SizeY - FootballHud::PowerBarBottom * UiScale;

	const float Fraction = KickComponent->GetChargeFraction();
	const bool bCharging = KickComponent->IsCharging();

	// Dim while idle so it does not shout, bright and warm as the power climbs.
	const FLinearColor FillColor = bCharging
		? FLinearColor(FMath::Lerp(0.4f, 1.f, Fraction), FMath::Lerp(1.f, 0.45f, Fraction), 0.25f, 0.95f)
		: FLinearColor(1.f, 1.f, 1.f, 0.18f);

	DrawBar(X, Y, Width, Height, bCharging ? Fraction : 0.f, FillColor, FootballHud::BarBackColor);

	const FString Label = bCharging
		? Format(ETextId::PowerFormat, { FString::Printf(TEXT("%.2f"), KickComponent->GetPowerScale()) })
		: FString(Tr(ETextId::HoldToCharge));
	const float Scale = TextSize();
	DrawCenteredText(Label, bCharging ? FootballHud::TextColor : FootballHud::DimTextColor,
		(float)Canvas->SizeX * 0.5f, Y - GetLineHeight(SmallFont(), Scale) - 4.f * UiScale, SmallFont(), Scale);
}

void AFootballHUD::DrawLives(const FFootballDrillHudState& DrillState)
{
	// Top right corner: the label, then one box per life, all right-aligned.
	const float Size = FootballHud::LifeSize * UiScale;
	const float Gap = FootballHud::LifeGap * UiScale;
	const float Right = (float)Canvas->SizeX - FootballHud::Margin * UiScale;
	const float Top = FootballHud::Margin * UiScale;

	const float LabelHeight = DrawRightAlignedText(Tr(ETextId::Lives), FootballHud::CornerTextColor, Right, Top, SmallFont(), TextSize());
	const float Y = Top + LabelHeight + 6.f * UiScale;

	const int32 MaxLives = FMath::Max(DrillState.MaxLives, 1);
	const float Left = Right - (float)MaxLives * Size - (float)(MaxLives - 1) * Gap;

	for (int32 Index = 0; Index < MaxLives; ++Index)
	{
		const float BoxX = Left + (float)Index * (Size + Gap);
		if (Index < DrillState.LivesLeft)
		{
			DrawRect(FootballHud::LifeColor, BoxX, Y, Size, Size);
		}
		else
		{
			DrawOutline(BoxX, Y, Size, Size, FMath::Max(1.f, 2.f * UiScale), FootballHud::LostLifeColor);
		}
	}
}

void AFootballHUD::DrawClock(const FFootballDrillHudState& DrillState)
{
	if (!DrillState.bOnTheClock)
	{
		return;
	}

	const float Width = FootballHud::ClockBarWidth * UiScale;
	const float Height = FootballHud::ClockBarHeight * UiScale;
	const float X = ((float)Canvas->SizeX - Width) * 0.5f;
	const float Y = (float)Canvas->SizeY * 0.5f + 46.f * UiScale;

	const float Fraction = FMath::Clamp(DrillState.DecisionTimeLeft / DrillState.DecisionTimeTotal, 0.f, 1.f);

	// Green with time to spare, red when it is nearly gone.
	const FLinearColor FillColor = (Fraction > 0.35f)
		? FLinearColor(0.35f, 0.95f, 0.45f, 0.9f)
		: FLinearColor(1.f, 0.3f, 0.2f, 0.95f);

	DrawBar(X, Y, Width, Height, Fraction, FillColor, FootballHud::BarBackColor);
	DrawCenteredText(Format(ETextId::SecondsFormat, { FString::Printf(TEXT("%.1f"), DrillState.DecisionTimeLeft) }), FillColor,
		(float)Canvas->SizeX * 0.5f, Y + Height + 4.f * UiScale, SmallFont(), TextSize());
}

void AFootballHUD::DrawScore(const FFootballDrillHudState& DrillState)
{
	const float X = FootballHud::Margin * UiScale;
	const float Y = FootballHud::Margin * UiScale;

	const FString Line = Format(ETextId::ScoreFormat,
		{ DrillState.Level + 1, GetStageName(DrillState.Level, DrillState.bEndlessLevel), DrillState.PassesCompleted });
	const float Scale = TextSize();
	DrawTextSharp(Line, FootballHud::CornerTextColor, X, Y, MediumFont(), Scale);

	DrawTextSharp(Tr(ETextId::PlayHint), FootballHud::CornerDimTextColor,
		X, Y + GetLineHeight(MediumFont(), Scale) + 4.f * UiScale, SmallFont(), Scale);
}

void AFootballHUD::DrawResultMessage(const FFootballDrillHudState& DrillState)
{
	if (DrillState.ResultMessageTimeLeft <= 0.f)
	{
		return;
	}
	const FString Message = GetDrillMessage(DrillState.ResultMessage, DrillState.ResultLivesLeft);
	if (Message.IsEmpty())
	{
		return;
	}

	// Fades out over the last second.
	const float Alpha = FMath::Clamp(DrillState.ResultMessageTimeLeft, 0.f, 1.f);
	const FLinearColor Color(DrillState.ResultColor.R, DrillState.ResultColor.G, DrillState.ResultColor.B, Alpha);

	DrawCenteredText(Message, Color, (float)Canvas->SizeX * 0.5f, (float)Canvas->SizeY * 0.26f, LargeFont(), TextSize(1.3f));
}

void AFootballHUD::DrawGameOver(const FFootballDrillHudState& DrillState)
{
	// Dim the whole screen, then the result in the middle.
	DrawRect(FootballHud::PanelColor, 0.f, 0.f, (float)Canvas->SizeX, (float)Canvas->SizeY);

	const float CenterX = (float)Canvas->SizeX * 0.5f;
	float Y = (float)Canvas->SizeY * 0.34f;

	Y += DrawCenteredText(Tr(ETextId::OutOfLives), FLinearColor(1.f, 0.35f, 0.25f, 1.f), CenterX, Y, LargeFont(), TextSize(2.f));
	Y += 14.f * UiScale;

	Y += DrawCenteredText(Format(ETextId::PassesCompletedFormat, { DrillState.PassesCompleted }),
		FLinearColor::White, CenterX, Y, LargeFont(), TextSize(1.4f));
	Y += 10.f * UiScale;

	Y += DrawCenteredText(Format(ETextId::ReachedLevelFormat, { DrillState.Level + 1, GetStageName(DrillState.Level, DrillState.bEndlessLevel) }),
		FootballHud::DimTextColor, CenterX, Y, MediumFont(), TextSize());
	Y += 24.f * UiScale;

	DrawCenteredText(Format(ETextId::RestartFormat, { DrillState.RestartKeyName }), FootballHud::TextColor, CenterX, Y, MediumFont(), TextSize(1.1f));
}
