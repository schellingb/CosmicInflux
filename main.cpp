/*
  Cosmic Influx
  Copyright (C) 2017 Bernhard Schelling

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include <ZL_Application.h>
#include <ZL_Display.h>
#include <ZL_Display3D.h>
#include <ZL_Audio.h>
#include <ZL_Font.h>
#include <ZL_Input.h>
#include <ZL_Surface.h>
#include <ZL_Particles.h>
#include <ZL_SynthImc.h>

#include <iostream>
#include <map>
#include <vector>
#include <list>
#include <algorithm>
using namespace std;

static ZL_Material matShip;
static ZL_Mesh mshShip, mshPlanet, mshSky, mshSun;
static ZL_Camera Camera;
static ZL_RenderList RenderList;
static ZL_Font fntMain;
static ZL_Surface srfLudumDare, srfShip;
static const float PowerStart = 100.f;
static const float GoalDistance = 150.f;
static ZL_ParticleEffect prtExplosion;
static ZL_TextBuffer TextBuffer;
extern ZL_SynthImcTrack sndSong, sndWin, sndLose;
extern ZL_Sound sndBlip;
static enum eGameMode { MODE_INIT, MODE_INTRO, MODE_RUNNING, MODE_SCANNING, MODE_LANDED, MODE_PAUSE, MODE_WIN, MODE_LOSE } Mode;
static enum eFadeMode { FADE_NONE, FADE_STARTUP, FADE_TOGAME, FADE_BACKTOTITLE, FADE_QUIT } FadeMode;
static float FadeStart;
static bool FadeIn;
static ticks_t EndTicks;

static struct sPlayer
{
	ZL_Vector3 Pos, Dir;
	float Distance;
	float Power;
} Player;

struct sPlanet
{
	ZL_Matrix Mtx;
	ZL_Material Mat;
	ZL_Color Col;
	float Radius;
	int ChancePower, ChanceEnemy;
	int GainByPower, LoseByEnemy;
	bool IsHome;
};

static vector<sPlanet> Planets;
static ZL_Light Suns[2];
static ZL_Light* SunList[2] = { &Suns[0], &Suns[1] };
static sPlanet* ScanPlanet;
static sPlanet* LandedPlanet;
static sPlanet* TravelPlanet;
static sPlanet* LastTravelPlanet;
static ZL_NameID nmFade = "fade";
static const float TravelDrainFactor = 4.f;

static struct sCosmicInflux : public ZL_Application
{
	sCosmicInflux() : ZL_Application(60) { }

	virtual void Load(int argc, char *argv[])
	{
		if (!ZL_Application::LoadReleaseDesktopDataBundle()) return;
		if (!ZL_Display::Init("Cosmic Influx", 1280, 720, ZL_DISPLAY_ALLOWRESIZEHORIZONTAL | ZL_DISPLAY_DEPTHBUFFER)) return;
		ZL_Display::SetAA(true);
		ZL_Display3D::Init(2);
		ZL_Audio::Init();
		ZL_Input::Init();

		fntMain = ZL_Font("Data/vipond_chubby.ttf.zip", 120).SetCharSpacing(5.5f);
		TextBuffer = ZL_TextBuffer(fntMain);

		srfLudumDare = ZL_Surface("Data/ludumdare.png").SetDrawOrigin(ZL_Origin::BottomRight);

		using namespace ZL_MaterialModes;
		matShip = ZL_Material(MM_VERTEXCOLOR | MM_LIT);

		mshPlanet = ZL_Mesh::BuildSphere(1, 63).SetMaterial(0, ZL_Material(MM_DIFFUSEFUNC | MR_TEXCOORD | MM_LIT | MM_SPECULARSTATIC,
			ZL_GLSL_IMPORTSNOISE()
			"uniform vec3 cola,colb,colw,colc;"
			"uniform float facs,facw,facc,fade;"
			"vec4 CalcDiffuse()"
			"{"
				"float s = clamp((snoise((" Z3V_TEXCOORD "+facs)*facs)-.5),0.,1.);"
				"float w = clamp((snoise((" Z3V_TEXCOORD "+facw)*facw)-.5)*3.,0.,1.);"
				"float c = clamp((snoise((" Z3V_TEXCOORD "+facc)*facc)-.5)*2.,0.,1.);"
				"float d = snoise((" Z3V_TEXCOORD "+299.)*299.)*.3;"
				"return vec4(mix(vec3(0.), mix(mix(mix(mix(cola,colb, s), vec3(0.,0.,0.), d), colw, w), colc, c), fade), 1.);"
			"}"
		));
		mshPlanet.GetMaterial().SetUniformFloat(Z3U_SPECULAR, .4f);
		mshPlanet.GetMaterial().SetUniformFloat(Z3U_SHININESS, 1.f);
		mshPlanet.GetMaterial().SetUniformFloat(nmFade, 1.f);
		mshPlanet.GetMaterial().SetUniformVec3("colc", ZL_Color::White);

		mshSky = ZL_Mesh::BuildSphere(50, 20, true).SetMaterial(0, ZL_Material(MM_DIFFUSEFUNC | MR_TEXCOORD,
			ZL_GLSL_IMPORTSNOISE()
			"vec4 CalcDiffuse()"
			"{"
				"float s = clamp((snoise(" Z3V_TEXCOORD "*250.0)-.95)*15.,0.,1.);"
				"return vec4(s,s,s,1);"
			"}"
		));

		mshSun = ZL_Mesh::BuildSphere(1, 23).SetMaterial(0, ZL_Material(MM_STATICCOLOR).SetUniformVec4(Z3U_COLOR, ZL_Color::Orange));
		Suns[0].SetPosition(ZLV3(15,0,20)).SetFalloff(80);
		Suns[1].SetPosition(ZLV3(-15,0,120)).SetFalloff(80);

		prtExplosion = ZL_ParticleEffect(900);
		prtExplosion.AddParticleImage(ZL_Surface("Data/smoke.png"), 1000);
		prtExplosion.AddBehavior(new ZL_ParticleBehavior_LinearMove(300, 20));
		prtExplosion.AddBehavior((new ZL_ParticleBehavior_LinearColor())->AddColorStart(ZL_Color::Red)->AddColorStart(ZL_Color::Orange)->AddColorStart(ZL_Color::Yellow)->AddColorEnd(ZL_Color::Black));
		prtExplosion.AddBehavior(new ZL_ParticleBehavior_LinearImageProperties(.5f, .0f, 2.5f, .1f));

		Intro();
		FadeTo(FADE_STARTUP);
	}

	void FadeTo(eFadeMode NewFadeMode)
	{
		if (FadeMode) return;
		FadeMode = NewFadeMode;
		ZL_Input::SetLock(1);
		FadeStart = -1;
		FadeIn = (FadeMode == FADE_STARTUP);
	}

	static void Intro()
	{
		Start();
		Mode = MODE_INTRO;
		for (vector<sPlanet>::iterator it = Planets.begin(); it != Planets.end(); ++it)
		{
			it->Mat.SetUniformFloat(nmFade, 1.f);
		}
		sndSong.Play();
		Player.Pos = ZL_Vector3(0, 0, 90.f);
	}

	static void Start()
	{
		sndSong.Stop();

		if (Mode != MODE_INTRO)
		{
			ZL_String ShipTexture = ZL_String::format("Data/ship%d.png", RAND_INT_RANGE(1,3));
			srfShip = ZL_Surface(ShipTexture).SetOrigin(ZL_Origin::Center).SetScale(2.f);
			mshShip = ZL_Mesh::BuildExtrudePixels(.05f, .1f, ShipTexture, matShip, false, true, .5f, ZL_Matrix::MakeRotateX(PIHALF)*ZL_Matrix::MakeRotateY(-PIHALF));
		}

		Player.Pos = ZL_Vector3(0.f);
		Player.Power = PowerStart;
		Mode = MODE_RUNNING;
		AdvanceToGoal();

		Planets.clear();
		for (float i = 0; i < GoalDistance - 7.f; i += RAND_RANGE(1,10))
		{
			sPlanet p;
			ZL_Color cola = RAND_COLOR, colb = RAND_COLOR;
			p.Col = (cola + colb) * .5f;
			p.ChancePower = RAND_INT_RANGE(0, 10)*10;
			p.ChanceEnemy = RAND_INT_RANGE(0, 10)*10;
			p.GainByPower = (p.ChancePower < RAND_INT_RANGE(0,100) ? 0 : RAND_INT_RANGE(10,100));
			p.LoseByEnemy = (p.ChanceEnemy < RAND_INT_RANGE(0,100) ? 0 : RAND_INT_RANGE(10,100));
			p.Radius = RAND_RANGE(.25,1.5);
			p.Mtx = ZL_Matrix::MakeRotateX(PIHALF) *  ZL_Matrix::MakeRotateY(RAND_RANGE(PI,PI2)) * ZL_Matrix::MakeTranslateScale(ZLV3(RAND_RANGE(1,4)*RAND_SIGN, RAND_RANGE(1,4)*RAND_SIGN, i), ZL_Vector3(p.Radius));
			p.Mat = mshPlanet.GetMaterial().MakeNewMaterialInstance();
			p.Mat.SetUniformFloat(nmFade, 1.f);
			p.Mat.SetUniformVec3("cola", cola);
			p.Mat.SetUniformVec3("colb", colb);
			p.Mat.SetUniformVec3("colw", RAND_COLOR);
			p.Mat.SetUniformFloat("facs", RAND_RANGE(1,50));
			p.Mat.SetUniformFloat("facw", RAND_RANGE(1,5));
			p.Mat.SetUniformFloat("facc", RAND_RANGE(3,10));
			p.IsHome = false;
			Planets.push_back(p);
		}
		sPlanet Home;
		Home.Col = ZL_Color::Blue;
		Home.Radius = 1.f;
		Home.Mtx = ZL_Matrix::MakeRotateX(PIHALF) *  ZL_Matrix::MakeRotateY(RAND_RANGE(PI,PI2)) * ZL_Matrix::MakeTranslateScale(ZLV3(0, -1.f, GoalDistance + 4.f), ZL_Vector3(Home.Radius));
		Home.Mat = mshPlanet.GetMaterial().MakeNewMaterialInstance();
		Home.Mat.SetUniformFloat(nmFade, 1.f);
		Home.Mat.SetUniformVec3("cola", ZL_Color::Green);
		Home.Mat.SetUniformVec3("colb", ZL_Color::Brown);
		Home.Mat.SetUniformVec3("colw", ZL_Color::Blue);
		Home.Mat.SetUniformFloat("facs", 25.f);
		Home.Mat.SetUniformFloat("facw", 3.f);
		Home.Mat.SetUniformFloat("facc", 5.f);
		Home.IsHome = true;
		Planets.push_back(Home);
	}

	static void AdvanceToGoal()
	{
		if (ZL_Vector(-Player.Pos.x, -Player.Pos.y).GetLengthSq() > 0.5f && Mode != MODE_RUNNING)
		{
			Player.Dir = ZL_Vector3(-Player.Dir.x, -Player.Dir.y, Player.Dir.z);
			ZL_Vector3 DirDirect = ZL_Vector3(-Player.Pos.x, -Player.Pos.y).Norm();
			float Dot = Player.Dir | DirDirect;
			Player.Distance = ZL_Vector(-Player.Pos.x, -Player.Pos.y).GetLength() / (Dot > 0.01f ? Dot : 1.f);
		}
		else
		{
			Player.Pos = ZL_Vector3(0, 0, Player.Pos.z);
			Player.Dir = ZL_Vector3::Forward;
			Player.Distance = GoalDistance - Player.Pos.z;
		}
	}

	virtual void AfterFrame()
	{
		if (ZL_Input::Up(ZLK_ESCAPE)) FadeTo(Mode == MODE_INTRO ?  FADE_QUIT : FADE_BACKTOTITLE);

		if (Mode == MODE_RUNNING)
		{
			float Elapsed = ZLELAPSEDF(2);
			if (TravelPlanet) Elapsed *= 2;
			#if defined(ZILLALOG)
			if (ZL_Display::KeyDown[ZLK_LCTRL]) Elapsed *= 50.f;
			#endif
			float DrainFactor = ZL_Vector3(Player.Dir.x * TravelDrainFactor, Player.Dir.y * TravelDrainFactor, Player.Dir.z).GetLength();
			float MoveDistance = ZL_Math::Min(ZL_Math::Min(Elapsed, Player.Power / DrainFactor), Player.Distance);
			Player.Pos += Player.Dir * MoveDistance;
			Player.Distance -= MoveDistance;
			Player.Power -= MoveDistance * DrainFactor;
			if (Player.Distance <= SMALL_NUMBER) Player.Distance = 0;
			if (!Player.Distance && TravelPlanet)
			{
				sndBlip.Play();
				Mode = MODE_LANDED;
				LandedPlanet = TravelPlanet;
				Player.Power = ZL_Math::Clamp(Player.Power + LandedPlanet->GainByPower - LandedPlanet->LoseByEnemy, 0.f, PowerStart);
			}
			if (Player.Power <= SMALL_NUMBER && Mode == MODE_RUNNING)
			{
				Mode = MODE_LOSE;
				EndTicks = ZLTICKS;
				sndLose.Play();
				const float SwayAmount = ZL_Math::Clamp01((2.f - (ZL_Math::Abs(Player.Pos.x) + ZL_Math::Abs(Player.Pos.y))) / 2.f);
				const ZL_Vector3 Sway = ZL_Vector3(ssin(Player.Pos.z*.5f),scos(Player.Pos.z*2.25f)*.3f,0) * SwayAmount;
				for (int i = 0; i < 300; i+= 10) prtExplosion.Spawn(10, Camera.WorldToScreen(Player.Pos + Sway), i);
				Player.Power = 0;
			}
			else if (!Player.Distance && Mode == MODE_RUNNING)
			{
				AdvanceToGoal();
				if (Player.Distance < .2f)
				{
					sndWin.Play();
					Mode = MODE_WIN;
					EndTicks = ZLTICKS;
				}
			}
		}

		float ShipSwayAmount = ZL_Math::Clamp01((2.f - (ZL_Math::Abs(Player.Pos.x) + ZL_Math::Abs(Player.Pos.y))) / 2.f);
		ZL_Vector3 ShipSway = ZL_Vector3(ssin(Player.Pos.z*.5f),scos(Player.Pos.z*2.25f)*.3f,0) * ShipSwayAmount;
		ZL_Vector3 CameraOffset = ZLV3(-1 + Player.Pos.x, 1, -3);
		if (Mode == MODE_INTRO)
		{
			ZL_Vector CameraXZ = ZL_Vector(CameraOffset.x, CameraOffset.z).Rotate(ZLSECONDS*.5f);
			CameraOffset = ZL_Vector3(CameraXZ.x, CameraOffset.y, CameraXZ.y) * .5f;
			ShipSway = ZL_Vector3::Zero;
		}
		Camera.SetPosition(Player.Pos + CameraOffset);
		Camera.SetDirection(-CameraOffset.VecNorm());

		sPlanet* HighlightPlanet = NULL;
		ZL_Vector HighlightPlanetScreen;
		if (Mode == MODE_INTRO)
		{
		}
		else if (ScanPlanet)
		{
			HighlightPlanet = ScanPlanet;
			HighlightPlanetScreen = Camera.WorldToScreen(ScanPlanet->Mtx.GetOrigin());
		}
		else if (!LandedPlanet && Player.Power)
		{
			float ClosestDistSq = S_MAX;
			for (vector<sPlanet>::iterator it = Planets.begin(); it != Planets.end(); ++it)
			{
				const float itZDist = it->Mtx.GetOrigin().z - Player.Pos.z;
				if (itZDist < -2.f || itZDist > 60.f) continue;
				const bool IsTravel = (&*it == TravelPlanet || &*it == LastTravelPlanet);
				if (itZDist > 30.f) it->Mat.SetUniformFloat(nmFade, ZL_Math::Clamp01(ZL_Math::InverseLerp(45.f, 30.f, itZDist)));
				else if (!IsTravel && itZDist <  4.f) it->Mat.SetUniformFloat(nmFade, ZL_Math::Clamp01(ZL_Math::InverseLerp(1.f, 4.f, itZDist)));
				else it->Mat.SetUniformFloat(nmFade, 1.f);
				if (!IsTravel && (itZDist < 3.f || itZDist > 35.f || it->IsHome)) continue;
				const float distZ = 25.f - itZDist;
				const ZL_Vector PlanetOnScreen = Camera.WorldToScreen(it->Mtx.GetOrigin());
				const float DistSq = PlanetOnScreen.GetDistanceSq(ZL_Input::Pointer()) + (distZ*distZ*3);
				if (DistSq > ClosestDistSq) continue;
				HighlightPlanet = &*it;
				ClosestDistSq = DistSq;
				HighlightPlanetScreen = PlanetOnScreen;
			}
		}

		RenderList.Reset();
		for (vector<sPlanet>::iterator it = Planets.begin(); it != Planets.end(); ++it)
		{
			const float itZDist = it->Mtx.GetOrigin().z - Player.Pos.z;
			if (Mode != MODE_INTRO && (itZDist < -2.f || itZDist > 60.f)) continue;
			mshPlanet.SetMaterial(it->Mat);
			RenderList.Add(mshPlanet, it->Mtx);
		}
		RenderList.Add(mshSun, ZL_Matrix::MakeTranslate(Suns[0].GetPosition()));
		RenderList.Add(mshSun, ZL_Matrix::MakeTranslate(Suns[1].GetPosition()));
		if (Player.Power) RenderList.Add(mshShip, ZL_Matrix::MakeTranslate(Player.Pos + ShipSway));
		RenderList.Add(mshSky, ZL_Matrix::MakeTranslate(Player.Pos));
		ZL_Display3D::DrawListWithLights(RenderList, Camera, SunList, 2);

		if (Mode == MODE_INTRO)
		{
			DrawText(ZLCENTER + ZLV(-100, 250), "COSMIC", 1.f, ZL_Origin::Center);
			DrawText(ZLCENTER + ZLV(100, 150), "INFLUX", 1.f, ZL_Origin::Center);
			
			DrawText(ZLV(ZLHALFW, ZLHALFH-160.f), "CLICK TO START", .3f, ZL_Origin::Center);
			DrawText(ZLV(ZLHALFW, ZLHALFH-230.f), "Visit dangerous planets to gather enough power to reach home!", .2f, ZL_Origin::Center);
			DrawText(ZLV(ZLHALFW, ZLHALFH-270.f), "Press Alt-Enter for fullscreen", .2f, ZL_Origin::Center);
			DrawText(ZLV(25.f, 17.f), "c 2017 Bernhard Schelling - Nukular Design", .3f, ZL_Origin::TopLeft);
			srfLudumDare.Draw(ZLFROMW(10), 10);

			if (ZL_Input::Clicked())
			{
				sndBlip.Play();
				FadeTo(FADE_TOGAME);
			}
		}
		else
		{
			if (!Player.Power) prtExplosion.Draw();

			if (HighlightPlanet)
			{
				const ZL_Vector3 WorldCameraRight = Camera.GetVPMatrix().GetInverted().TransformDirection(ZL_Vector3::Right).Norm();
				const float ClosestPlanetRadius = Camera.WorldToScreen(HighlightPlanet->Mtx.GetOrigin() + WorldCameraRight * HighlightPlanet->Radius).GetDistance(HighlightPlanetScreen);
				const ZL_Rectf RecPlanet(HighlightPlanetScreen, ClosestPlanetRadius + 5.f);
				if (!ScanPlanet && ZL_Input::Clicked(RecPlanet))
				{
					sndBlip.Play();
					Mode = MODE_SCANNING;
					ScanPlanet = HighlightPlanet;
				}
				if (ScanPlanet) ZL_Display::DrawCircle(HighlightPlanetScreen, ClosestPlanetRadius + 5.f, ZL_Color::Green, ZLRGBA(0,1,0,.3));
				else if (ZL_Input::Hover(RecPlanet)) ZL_Display::DrawCircle(HighlightPlanetScreen, ClosestPlanetRadius + 5.f, ZL_Color::Cyan, ZLRGBA(0,1,1,.4));
				else HighlightPlanet = NULL;
			}

			const float PowerAmountX = ZL_Math::Lerp(152, ZLFROMW(8), ZL_Math::InverseLerp(0.f, PowerStart, Player.Power));
			ZL_Display::DrawRect(-10, ZLFROMH(30), ZLFROMW(-10), ZLFROMH(-10), ZLWHITE, ZLBLACK);
			fntMain.Draw(44, ZLFROMH(23), "POWER:", .2f);
			ZL_Display::DrawRect(152, ZLFROMH(22), PowerAmountX, ZLFROMH(8), ZL_Color::Cyan, ZL_Color::Blue);
			ZL_Display::DrawRect(150, ZLFROMH(24), ZLFROMW(6), ZLFROMH(6), ZLWHITE);
			fntMain.Draw(157, ZLFROMH(21), ZL_String::format("%d", (int)(Player.Power+.5f)), .135f);

			const float ShipPosTimelineX = ZL_Math::Lerp(150, ZLFROMW(15), ZL_Math::InverseLerp(0.f, GoalDistance, Player.Pos.z));
			ZL_Display::DrawRect(-10,        -10 , ZLFROMW(-10),          30 , ZLWHITE, ZLBLACK);
			fntMain.Draw(6, 7, "PROGRESS:", .2f);
			ZL_Display::DrawLine(150, 15, ZLFROMW(15), 15, ZLWHITE);
			for (vector<sPlanet>::iterator it = Planets.begin(); it != Planets.end(); ++it)
			{
				const float PlanetTimelineX = ZL_Math::Lerp(150, ZLFROMW(15), ZL_Math::InverseLerp(0.f, GoalDistance, it->Mtx.GetOrigin().z));
				if (&*it == HighlightPlanet) ZL_Display::FillCircle(PlanetTimelineX, 15, 3 + 4 * it->Radius, ZL_Color::Cyan);
				if (&*it == ScanPlanet)      ZL_Display::FillCircle(PlanetTimelineX, 15, 3 + 4 * it->Radius, ZL_Color::Green);
				if (&*it == TravelPlanet)    ZL_Display::FillCircle(PlanetTimelineX, 15, 3 + 4 * it->Radius, ZL_Color::Yellow);
				ZL_Display::DrawCircle(PlanetTimelineX, 15, 4 * it->Radius, ZL_Color::Gray, it->Col);
			}
			ZL_Display::DrawLine(150, 5, 150, 25, ZLWHITE);
			ZL_Display::DrawCircle(ZLFROMW(15), 15, 10, ZL_Color::White, ZL_Color::Black);
			ZL_Display::DrawCircle(ZLFROMW(15), 15, 7, ZL_Color::White, ZL_Color::Black);
			ZL_Display::DrawCircle(ZLFROMW(15), 15, 4, ZL_Color::White, ZL_Color::Black);
			ZL_Display::FillCircle(ShipPosTimelineX, 15, 15, ZLLUMA(.3,.6));
			for (int i = 0; i < 5; i++) srfShip.Draw(ShipPosTimelineX, 15);

			if (Mode == MODE_SCANNING)
			{
				const ZL_Rectf RecMenu(ZLCENTER, ZLV(300, 150));
				const ZL_Vector3 TravelTarget = ScanPlanet->Mtx.GetOrigin() + ZLV3(0, ssign(ScanPlanet->Mtx.GetOrigin().y) * -ScanPlanet->Radius, -ScanPlanet->Radius - .2f);
				const ZL_Vector3 TravelDelta = (TravelTarget - Player.Pos);
				const float TravelDistance = TravelDelta.GetLength();
				const ZL_Vector3 TravelDir = TravelDelta / TravelDistance;
				const float TravelDeltaDrain = ZL_Vector3(TravelDir.x * TravelDrainFactor, TravelDir.y * TravelDrainFactor, TravelDir.z).GetLength();
				const float TravelDrainTotal = TravelDeltaDrain * TravelDistance * 2.f;
				const float TravelDrainExtra = TravelDrainTotal - ((TravelTarget.z - Player.Pos.z) * 2.f);
				ZL_Display::DrawRect(RecMenu, ZLWHITE, ZLLUMA(1, .5));
				DrawText(RecMenu.HighLeft() + ZLV(300,  -35), "Planet Scan", .25f, ZL_Origin::BottomCenter);
				ZL_Display::DrawLine(RecMenu.HighLeft() + ZLV(20, -50), RecMenu.HighRight() + ZLV(-20, -50), ZLWHITE);
				DrawText(RecMenu.HighLeft() + ZLV(30,  -80), "Chance of Power Supply:", .25f);
				DrawText(RecMenu.HighLeft() + ZLV(30, -110), "Chance of Enemy:", .25f);
				DrawText(RecMenu.HighLeft() + ZLV(30, -160), "Distance:", .25f);
				DrawText(RecMenu.HighLeft() + ZLV(30, -200), "Required Extra Travel Power:", .25f);
				DrawText(RecMenu.HighRight() + ZLV(-30,  -80), ZL_String::format("%d", ScanPlanet->ChancePower), .25f, ZL_Origin::BottomRight);
				DrawText(RecMenu.HighRight() + ZLV(-30, -110), ZL_String::format("%d", ScanPlanet->ChanceEnemy), .25f, ZL_Origin::BottomRight);
				DrawText(RecMenu.HighRight() + ZLV(-30, -160), ZL_String::format("%d", (int)(TravelDistance + .5f)), .25f, ZL_Origin::BottomRight);
				DrawText(RecMenu.HighRight() + ZLV(-30, -200), ZL_String::format("%d", (int)(TravelDrainExtra - .5f)), .25f, ZL_Origin::BottomRight);
				if (Button(ZL_Rectf(RecMenu.LowLeft() +  ZLV(150, 50), ZLV(100, 30)), "VISIT"))
				{
					sndBlip.Play();
					TravelPlanet = ScanPlanet;
					Player.Distance = TravelDistance;
					Player.Dir = TravelDir;
					ScanPlanet = NULL;
					Mode = MODE_RUNNING;
				}
				else if (Button(ZL_Rectf(RecMenu.LowRight() +  ZLV(-150, 50), ZLV(100, 30)), (ScanPlanet == TravelPlanet ? "ABORT" : "IGNORE")))
				{
					sndBlip.Play();
					if (ScanPlanet == TravelPlanet) { LastTravelPlanet = TravelPlanet; TravelPlanet = NULL; AdvanceToGoal(); }
					ScanPlanet = NULL;
					Mode = MODE_RUNNING;
				}
				else if (ZL_Input::ClickedOutside(RecMenu))
				{
					sndBlip.Play();
					ScanPlanet = NULL;
					Mode = MODE_RUNNING;
				}
			}

			if (Mode == MODE_LANDED)
			{
				const ZL_Rectf RecMenu(ZLCENTER, ZLV(300, 150));
				ZL_Display::DrawRect(RecMenu, ZLWHITE, ZLLUMA(1, .5));
				DrawText(RecMenu.HighLeft() + ZLV(300,  -35), "Planet Result", .25f, ZL_Origin::BottomCenter);
				ZL_Display::DrawLine(RecMenu.HighLeft() + ZLV(20, -50), RecMenu.HighRight() + ZLV(-20, -50), ZLWHITE);
				DrawText(RecMenu.HighLeft() + ZLV(30,  -80), "Found Power Supply:", .25f);
				DrawText(RecMenu.HighLeft() + ZLV(30, -110), "Power Lost in Battle:", .25f);
				DrawText(RecMenu.HighRight() + ZLV(-30,  -80), ZL_String::format("%d", LandedPlanet->GainByPower), .25f, ZL_Origin::BottomRight);
				DrawText(RecMenu.HighRight() + ZLV(-30, -110), ZL_String::format("%d", LandedPlanet->LoseByEnemy), .25f, ZL_Origin::BottomRight);
				if (Button(ZL_Rectf(RecMenu.LowLeft() +  ZLV(300, 50), ZLV(250, 30)), (Player.Power ? "CONTINUE" : "OOPS")) || ZL_Input::ClickedOutside(RecMenu))
				{
					sndBlip.Play();
					AdvanceToGoal();
					LastTravelPlanet = TravelPlanet; 
					TravelPlanet = NULL;
					LandedPlanet = NULL;
					Mode = MODE_RUNNING;
				}
			}

			if (Mode == MODE_WIN || Mode == MODE_LOSE)
			{
				ZL_Color BlackFade = ZLLUMA(0, ZL_Math::Clamp01(ZLSINCESECONDS(EndTicks+500))*.1f);
				for (float f = 1.f; f > .11f; f -= .05f)
				{
					ZL_Display::FillRect(ZL_Rectf(ZLCENTER, ZLCENTER*f), BlackFade);
				}
				if (Mode == MODE_WIN)  DrawText(ZLCENTER + ZLV(0, 250), "Congratulations!", .5f, ZL_Origin::Center);
				if (Mode == MODE_WIN)  DrawText(ZLCENTER + ZLV(0, 170), "You and your crew managed to get home!", .3f, ZL_Origin::Center);
				if (Mode == MODE_WIN)  DrawText(ZLCENTER + ZLV(0, 50), "YOU WIN", 1.f, ZL_Origin::Center);
				if (Mode == MODE_LOSE) DrawText(ZLCENTER + ZLV(0, 50), "GAME OVER", 1.f, ZL_Origin::Center);

				if (Button(ZL_Rectf(ZLCENTER + ZLV(0, -100), ZLV(200, 30)), "START NEW GAME"))
				{
					sndBlip.Play();
					Start();
				}
				if (Button(ZL_Rectf(ZLCENTER + ZLV(0, -200), ZLV(200, 30)), "RETURN TO TITLE"))
				{
					FadeTo(FADE_BACKTOTITLE);
				}
			}
		}

		if (FadeMode)
		{
			if (FadeStart < 0) FadeStart = ZLSECONDS;
			float t = ZL_Math::Min((ZLSECONDS - FadeStart) * 3.f, 1.f);
			ZL_Display::FillRect(0, 0, ZLWIDTH, ZLHEIGHT, ZLLUMA(0, (FadeIn ? 1.f - t : t)));
			if (t == 1.f && !FadeIn)
			{
				FadeIn = true;
				FadeStart = ZLSECONDS;
				if (FadeMode == FADE_TOGAME)      Start();
				if (FadeMode == FADE_BACKTOTITLE) Intro();
				if (FadeMode == FADE_QUIT)        ZL_Application::Quit();
			}
			else if (t == 1.f)
			{
				if (FadeMode == FADE_STARTUP) sndSong.Play();
				FadeMode = FADE_NONE;
				ZL_Input::RemoveLock();
			}
			if (!FadeIn && FadeMode == FADE_TOGAME)      sndSong.SetSongVolume(  0 + (int)((1.f-t) * 99.f));
			if ( FadeIn && FadeMode == FADE_BACKTOTITLE) sndSong.SetSongVolume(  0 + (int)((    t) * 99.f));
			if (!FadeIn && FadeMode == FADE_QUIT)        sndSong.SetSongVolume(-30 + (int)((1.f-t) * 99.f));
		}
	}

	static void DrawText(const ZL_Vector &p, const char *text, scalar scale, ZL_Origin::Type draw_at_origin = ZL_Origin::BottomLeft, const ZL_Color& color = ZL_Color::White)
	{
		TextBuffer.SetText(text);
		for (int i = -4; i <= 4; i++) if (i) TextBuffer.Draw(p + ZLV(i/3, i%3), scale, ZLLUMA(0,.5), draw_at_origin);
		TextBuffer.Draw(p, scale, color, draw_at_origin);
	}

	static bool Button(const ZL_Rectf& Rec, const char* Text)
	{
		const ZL_Color Col = (ZL_Input::Held(Rec) ? ZL_Color::Green : (ZL_Input::Hover(Rec) ? ZL_Color::Yellow : ZLWHITE));
		ZL_Display::DrawRect(Rec, Col, ZLRGBA(Col.r, Col.g, Col.b, .5));
		DrawText(Rec.Center(), Text, .3f, ZL_Origin::Center, Col);
		return !!ZL_Input::Clicked(Rec);
	}
} CosmicInflux;

// SOUND EFFECT / MUSIC DATA --------------------------------------------

static const unsigned int IMCSONG_OrderTable[] = { 0x011000001, 0x011000002, 0x012000003, 0x012000004, };
static const unsigned char IMCSONG_PatternData[] = { 0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x44, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x57, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x54, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x57, 0x55, 0x54, 0x52, 0x50, 0, 0, 0, 0x50, 0, 0x50, 0, 0x50, 0, 0, 0,
	0x50, 0, 0x50, 0, 0x50, 0, 0x50, 0, 0x50, 0, 0x50, 0, 0x50, 0, 0x50, 0, 0, 0, 0, 0, 0, 0, 0x50, 0, 0, 0, 0, 0, 0, 0, 0x50, 0, };
static const unsigned char IMCSONG_PatternLookupTable[] = { 0, 4, 4, 4, 4, 4, 4, 6, };
static const TImcSongEnvelope IMCSONG_EnvList[] = { { 0, 256, 20, 8, 15, 255, true, 255, }, { 50, 100, 202, 24, 255, 255, true, 255, }, { 0, 256, 2, 27, 13, 255, false, 255, },
	{ 0, 256, 157, 25, 15, 255, false, 0, }, { 0, 256, 87, 8, 16, 255, true, 255, }, { 196, 256, 29, 8, 16, 255, true, 255, }, { 0, 128, 1046, 8, 16, 255, true, 255, },
	{ 0, 256, 93, 8, 16, 255, true, 255, }, { 196, 256, 52, 3, 21, 255, true, 255, }, { 0, 256, 260, 8, 16, 255, true, 255, }, { 0, 128, 435, 7, 8, 255, true, 255, },
	{ 0, 256, 325, 8, 16, 255, true, 255, }, { 0, 256, 653, 8, 8, 255, true, 255, }, { 0, 256, 2703, 8, 16, 255, true, 255, }, };
static TImcSongEnvelopeCounter IMCSONG_EnvCounterList[] = { { 0, 0, 256 }, { -1, -1, 256 }, { 1, 0, 50 }, { 2, 0, 18 },
	{ 3, 0, 2 }, { 4, 6, 256 }, { 5, 6, 256 }, { 4, 6, 256 }, { 6, 6, 128 }, { -1, -1, 400 }, { 7, 7, 256 }, { 8, 7, 244 },
	{ 9, 7, 256 }, { 10, 7, 127 }, { 11, 7, 256 }, { 12, 7, 256 }, { 13, 7, 256 }, };
static const TImcSongOscillator IMCSONG_OscillatorList[] = { { 7, 0, IMCSONGOSCTYPE_SINE, 0, -1, 116, 1, 1 }, { 8, 0, IMCSONGOSCTYPE_SINE, 0, -1, 144, 1, 1 },
	{ 7, 0, IMCSONGOSCTYPE_SINE, 0, -1, 68, 1, 1 }, { 9, 0, IMCSONGOSCTYPE_SQUARE, 0, 0, 30, 1, 1 }, { 7, 0, IMCSONGOSCTYPE_SAW, 0, 1, 36, 1, 1 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 }, { 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 }, { 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 }, { 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 }, { 5, 15, IMCSONGOSCTYPE_SINE, 6, -1, 98, 5, 6 },
	{ 7, 0, IMCSONGOSCTYPE_SINE, 6, -1, 200, 7, 8 }, { 5, 66, IMCSONGOSCTYPE_SINE, 7, -1, 98, 10, 11 }, { 7, 0, IMCSONGOSCTYPE_SINE, 7, -1, 255, 12, 13 },
	{ 6, 0, IMCSONGOSCTYPE_SINE, 7, -1, 192, 14, 15 }, { 9, 150, IMCSONGOSCTYPE_SINE, 7, -1, 255, 16, 1 }, { 8, 0, IMCSONGOSCTYPE_SINE, 7, 15, 255, 1, 1 }, };
static const TImcSongEffect IMCSONG_EffectList[] = { { 0, 0, 101, 0, IMCSONGEFFECTTYPE_FLANGE, 2, 0 }, { 53, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 1, 0 },
	{ 255, 165, 1, 0, IMCSONGEFFECTTYPE_RESONANCE, 3, 4 }, { 122, 0, 16536, 0, IMCSONGEFFECTTYPE_DELAY, 0, 0 }, { 18034, 463, 1, 6, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 1 },
	{ 189, 128, 1, 7, IMCSONGEFFECTTYPE_RESONANCE, 1, 1 }, { 58, 0, 2756, 7, IMCSONGEFFECTTYPE_DELAY, 0, 0 }, };
static unsigned char IMCSONG_ChannelVol[8] = { 80, 0, 0, 0, 0, 0, 80, 80 };
static const unsigned char IMCSONG_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 1, 9 };
static const bool IMCSONG_ChannelStopNote[8] = { true, false, false, false, false, false, true, true };
static TImcSongData imcDataIMCSONG = {
	/*LEN*/ 0x4, /*ROWLENSAMPLES*/ 5512, /*ENVLISTSIZE*/ 14, /*ENVCOUNTERLISTSIZE*/ 17, /*OSCLISTSIZE*/ 17, /*EFFECTLISTSIZE*/ 7, /*VOL*/ 100,
	IMCSONG_OrderTable, IMCSONG_PatternData, IMCSONG_PatternLookupTable, IMCSONG_EnvList, IMCSONG_EnvCounterList, IMCSONG_OscillatorList, IMCSONG_EffectList,
	IMCSONG_ChannelVol, IMCSONG_ChannelEnvCounter, IMCSONG_ChannelStopNote };
ZL_SynthImcTrack sndSong = ZL_SynthImcTrack(&imcDataIMCSONG);

static const unsigned int IMCWIN_OrderTable[] = { 0x011000001, 0x002000000, };
static const unsigned char IMCWIN_PatternData[] = { 0x50, 0, 0x50, 0x50, 0x50, 0, 0x54, 0, 0x50, 0, 0x52, 0, 0x55, 0, 0, 0, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0, 0, 0,
	0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x50, 0, 0x50, 0x50, 0x50, 0, 0x54, 0, 0x50, 0, 0x52, 0, 0x54, 0, 0, 0, };
static const unsigned char IMCWIN_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 3, };
static unsigned char IMCWIN_ChannelVol[8] = { 70, 0, 0, 0, 0, 0, 70, 70 };
static const unsigned char IMCWIN_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 1, 9 };
static const bool IMCWIN_ChannelStopNote[8] = { true, false, false, false, false, false, true, true };
static TImcSongData imcDataIMCWIN = {
	/*LEN*/ 0x2, /*ROWLENSAMPLES*/ 5512, /*ENVLISTSIZE*/ 14, /*ENVCOUNTERLISTSIZE*/ 17, /*OSCLISTSIZE*/ 17, /*EFFECTLISTSIZE*/ 7, /*VOL*/ 100,
	IMCWIN_OrderTable, IMCWIN_PatternData, IMCWIN_PatternLookupTable, IMCSONG_EnvList, IMCSONG_EnvCounterList, IMCSONG_OscillatorList, IMCSONG_EffectList,
	IMCWIN_ChannelVol, IMCWIN_ChannelEnvCounter, IMCWIN_ChannelStopNote };
ZL_SynthImcTrack sndWin = ZL_SynthImcTrack(&imcDataIMCWIN, false);

static const unsigned int IMCLOSE_OrderTable[] = { 0x011000001, 0x002000005, };
static const unsigned char IMCLOSE_PatternData[] = { 0x55, 0, 0x57, 0, 0x54, 0, 0x57, 0, 0x52, 0, 0, 0, 0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x55, 0, 0, 0, 0x54, 0, 0, 0, 0x52, 0, 0, 0, 0x50, 0, 0, 0, };
static const unsigned char IMCLOSE_PatternLookupTable[] = { 0, 5, 5, 5, 5, 5, 5, 7, };
static unsigned char IMCLOSE_ChannelVol[8] = { 70, 0, 0, 0, 0, 0, 70, 70 };
static const unsigned char IMCLOSE_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 1, 9 };
static const bool IMCLOSE_ChannelStopNote[8] = { true, false, false, false, false, false, true, true };
static TImcSongData imcDataIMCLOSE = {
	/*LEN*/ 0x2, /*ROWLENSAMPLES*/ 5512, /*ENVLISTSIZE*/ 14, /*ENVCOUNTERLISTSIZE*/ 17, /*OSCLISTSIZE*/ 17, /*EFFECTLISTSIZE*/ 7, /*VOL*/ 100,
	IMCLOSE_OrderTable, IMCLOSE_PatternData, IMCLOSE_PatternLookupTable, IMCSONG_EnvList, IMCSONG_EnvCounterList, IMCSONG_OscillatorList, IMCSONG_EffectList,
	IMCLOSE_ChannelVol, IMCLOSE_ChannelEnvCounter, IMCLOSE_ChannelStopNote };
ZL_SynthImcTrack sndLose = ZL_SynthImcTrack(&imcDataIMCLOSE, false);

static const unsigned int IMCBLIP_OrderTable[] = { 0x000000001, };
static const unsigned char IMCBLIP_PatternData[] = { 0x5B, 0x62, 0x60, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
static const unsigned char IMCBLIP_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCBLIP_EnvList[] = { { 0, 256, 65, 8, 16, 4, true, 255, }, };
static TImcSongEnvelopeCounter IMCBLIP_EnvCounterList[] = { { 0, 0, 256 }, { -1, -1, 256 }, };
static const TImcSongOscillator IMCBLIP_OscillatorList[] = { { 8, 0, IMCSONGOSCTYPE_SQUARE, 0, -1, 100, 1, 1 }, { 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 }, { 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 }, { 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 }, { 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 }, { 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 }, };
static unsigned char IMCBLIP_ChannelVol[8] = { 50, 0, 0, 0, 0, 0, 0, 0 };
static const unsigned char IMCBLIP_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCBLIP_ChannelStopNote[8] = { false, false, false, false, false, false, false, false };
static TImcSongData imcDataIMCBLIP = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 1, /*ENVCOUNTERLISTSIZE*/ 2, /*OSCLISTSIZE*/ 8, /*EFFECTLISTSIZE*/ 0, /*VOL*/ 100,
	IMCBLIP_OrderTable, IMCBLIP_PatternData, IMCBLIP_PatternLookupTable, IMCBLIP_EnvList, IMCBLIP_EnvCounterList, IMCBLIP_OscillatorList, NULL,
	IMCBLIP_ChannelVol, IMCBLIP_ChannelEnvCounter, IMCBLIP_ChannelStopNote };
ZL_Sound sndBlip = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCBLIP);
