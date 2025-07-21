//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================

// Triangle rendering, if any

#include "hud.h"
#include "cl_util.h"

// Triangle rendering apis are in gEngfuncs.pTriAPI

#include "const.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "triangleapi.h"
#include "Exports.h"

#include "particleman.h"
#include "tri.h"
extern IParticleMan* g_pParticleMan;

// [ap] Other dependencies
#include "com_model.h"
#include "studio.h"
#include "StudioModelRenderer.h"
#include "r_studioint.h"
#include <windows.h>
#include <GL/gl.h>
#include <ap_hud.h>
#include "pmtrace.h"
#include "pm_defs.h"
#include "pm_shared.h"

extern engine_studio_api_t IEngineStudio;

void DrawBoundingBoxEntGL(cl_entity_t* ent, float r, float g, float b, float a)
{
	if (!ent || !ent->model)
		return;

	Vector origin = ent->origin;
	Vector mins, maxs;

	if (ent->model->type == mod_brush)
	{
		if (ent->origin != Vector(0, 0, 0))
		{
			origin = ent->origin;
			mins = ent->model->mins + origin;
			maxs = ent->model->maxs + origin;
		}
		else
		{
			// Brush entity without origin — assume mins/maxs are world-space
			origin = (ent->model->mins + ent->model->maxs) * 0.5f;
			mins = ent->model->mins;
			maxs = ent->model->maxs;
		}
	}
	else if (ent->model->type == mod_studio)
	{
		// For studio models, we can try ent->origin as the base
		mins = ent->origin + ent->model->mins;
		maxs = ent->origin + ent->model->maxs;
		if (mins == maxs) {
			if (ent->curstate.mins == ent->curstate.maxs) {
				mins = ent->origin + Vector(-16, -16, 0);
				maxs = ent->origin + Vector(16, 16, 32);
			}
			else {
				mins = ent->origin + ent->curstate.mins;
				maxs = ent->origin + ent->curstate.maxs;
			}
		}
	}
	else
	{
		// fallback
		mins = ent->origin + Vector(-16, -16, 0);
		maxs = ent->origin + Vector(16, 16, 32);
	}

	Vector corners[8] = {
		{mins.x, mins.y, mins.z},
		{maxs.x, mins.y, mins.z},
		{maxs.x, maxs.y, mins.z},
		{mins.x, maxs.y, mins.z},
		{mins.x, mins.y, maxs.z},
		{maxs.x, mins.y, maxs.z},
		{maxs.x, maxs.y, maxs.z},
		{mins.x, maxs.y, maxs.z}};

	glPushAttrib(GL_ALL_ATTRIB_BITS);

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);

	glLineWidth(0.5f);
	glColor4f(r, g, b, a);

	glBegin(GL_LINES);
	// Bottom
	glVertex3fv(corners[0]);
	glVertex3fv(corners[1]);
	glVertex3fv(corners[1]);
	glVertex3fv(corners[2]);
	glVertex3fv(corners[2]);
	glVertex3fv(corners[3]);
	glVertex3fv(corners[3]);
	glVertex3fv(corners[0]);

	// Top
	glVertex3fv(corners[4]);
	glVertex3fv(corners[5]);
	glVertex3fv(corners[5]);
	glVertex3fv(corners[6]);
	glVertex3fv(corners[6]);
	glVertex3fv(corners[7]);
	glVertex3fv(corners[7]);
	glVertex3fv(corners[4]);

	// Verticals
	glVertex3fv(corners[0]);
	glVertex3fv(corners[4]);
	glVertex3fv(corners[1]);
	glVertex3fv(corners[5]);
	glVertex3fv(corners[2]);
	glVertex3fv(corners[6]);
	glVertex3fv(corners[3]);
	glVertex3fv(corners[7]);
	glEnd();

	glPopAttrib();
}

static void DrawBoundingBoxGL(const Vector& origin, const Vector& mins, const Vector& maxs, float r, float g, float b, float a)
{
	Vector corners[8] = {
		{origin.x + mins.x, origin.y + mins.y, origin.z + mins.z},
		{origin.x + maxs.x, origin.y + mins.y, origin.z + mins.z},
		{origin.x + maxs.x, origin.y + maxs.y, origin.z + mins.z},
		{origin.x + mins.x, origin.y + maxs.y, origin.z + mins.z},
		{origin.x + mins.x, origin.y + mins.y, origin.z + maxs.z},
		{origin.x + maxs.x, origin.y + mins.y, origin.z + maxs.z},
		{origin.x + maxs.x, origin.y + maxs.y, origin.z + maxs.z},
		{origin.x + mins.x, origin.y + maxs.y, origin.z + maxs.z}};

	glPushAttrib(GL_ALL_ATTRIB_BITS);

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glLineWidth(1.0f);
	glColor4f(r,g,b,a);

	glBegin(GL_LINES);
	// Bottom
	glVertex3fv(&corners[0].x);
	glVertex3fv(&corners[1].x);

	glVertex3fv(&corners[1].x);
	glVertex3fv(&corners[2].x);

	glVertex3fv(&corners[2].x);
	glVertex3fv(&corners[3].x);

	glVertex3fv(&corners[3].x);
	glVertex3fv(&corners[0].x);

	// Top
	glVertex3fv(&corners[4].x);
	glVertex3fv(&corners[5].x);

	glVertex3fv(&corners[5].x);
	glVertex3fv(&corners[6].x);

	glVertex3fv(&corners[6].x);
	glVertex3fv(&corners[7].x);

	glVertex3fv(&corners[7].x);
	glVertex3fv(&corners[4].x);

	// Vertical edges
	glVertex3fv(&corners[0].x);
	glVertex3fv(&corners[4].x);

	glVertex3fv(&corners[1].x);
	glVertex3fv(&corners[5].x);

	glVertex3fv(&corners[2].x);
	glVertex3fv(&corners[6].x);

	glVertex3fv(&corners[3].x);
	glVertex3fv(&corners[7].x);
	glEnd();

	glDisable(GL_BLEND);

	glPopAttrib();
}

static int lastLookedAtEntity = -1;

int GetLookedAtEntity()
{
	cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	if (!local || gEngfuncs.IsSpectateOnly())
		return -1;

	Vector viewAngles;
	gEngfuncs.GetViewAngles(viewAngles);

	Vector forward;
	AngleVectors(viewAngles, forward, nullptr, nullptr);
	
	Vector start = local->origin;
	// move z up to eye height
	start[2] = pmove->origin[2] + pmove->view_ofs[2];

	Vector end;
	VectorMA(start, 2048.0f, forward, end);

	pmtrace_t* trace = gEngfuncs.PM_TraceLine(start, end, PM_TRACELINE_ANYVISIBLE, 2, -1);

	if (!trace || trace->fraction == 1.0f || trace->ent <= 0)
		return -1;

	int physEntIndex = trace->ent;
	if (physEntIndex < 0 || physEntIndex > pmove->numphysent)
		return -1;

	int realEntIndex = pmove->physents[physEntIndex].info;
	cl_entity_t* ent = gEngfuncs.GetEntityByIndex(realEntIndex);
	if (!ent || !ent->model || !ent->model->name)
		return -1;

	const char* modelName = ent->model->name;

	if (strstr(modelName, "scientist") || strstr(modelName, "barney"))
		return realEntIndex;

	return -1;
}

/*
=================
HUD_DrawNormalTriangles

Non-transparent triangles-- add them here
=================
*/
void DLLEXPORT HUD_DrawNormalTriangles()
{
	//	RecClDrawNormalTriangles();

	gHUD.m_Spectator.DrawOverview();
}


/*
=================
HUD_DrawTransparentTriangles

Render any triangles with transparent rendermode needs here
=================
*/
void DLLEXPORT HUD_DrawTransparentTriangles()
{
	//	RecClDrawTransparentTriangles();
	
	// first we draw the triggers
	if (ap_hud_draw_triggers && ap_hud_draw_triggers->value != 0.0f) {
		
		for (const auto& zone : g_TriggerZones)
		{
			DrawBoundingBoxGL(zone.origin, zone.mins, zone.maxs, 1.0f, 0.0f, 1.0f, 0.5f);
		}
	}

	// then we print sequence numbers of entities
	int lookedAt = GetLookedAtEntity();

	if (lookedAt != lastLookedAtEntity)
	{
		if (lookedAt == -1) {
			//printf("Looking at nothing\n");
		}
		else
		{
			cl_entity_t* ent = gEngfuncs.GetEntityByIndex(lookedAt);
			printf("Looked-at entity %d pointer %p, modelindex %d\n", lookedAt, ent, ent->curstate.modelindex);
			printf("Model: %s, Sequence: %d\n", ent->model->name, ent->curstate.sequence);
		}
		lastLookedAtEntity = lookedAt;
	}



	// then we check for brushes with droppable items
	for (int i = 1; i < MAX_EDICTS; ++i)
	{
		cl_entity_t* ent = gEngfuncs.GetEntityByIndex(i);

		if (!ent)
			continue;

		// first we check for triggers to draw
		if (ap_hud_draw_triggers && ap_hud_draw_triggers->value != 0.0f && ent->model && ent->model->type == mod_brush)
		{
			// Check if this is a trigger brush
			if (ent->curstate.solid == SOLID_TRIGGER) // usually 2
			{
				Vector mins = ent->model->mins;
				Vector maxs = ent->model->maxs;

				Vector origin = ent->origin; // Usually zero or small offset

				//DrawTriggerVolume(ent);
				continue;
			}
		}

		// draw boxes with items, identified by custom flag
		if (ent->curstate.solid != SOLID_NOT && (ent->baseline.body == 250 || ent->curstate.body == 250))
		{
			DrawBoundingBoxEntGL(ent, 1.0f, 1.0f, 0.0f, 1.0f);
		}
		// draw recharging stations
		else if (ent->baseline.body == 249 || ent->curstate.body == 249)
		{
			DrawBoundingBoxEntGL(ent, 1.0f, 0.5f, 0.0f, 1.0f);
		}
		//otherwise we have to check for models existing
		else
		{
			int modelIndex = ent->curstate.modelindex;
			if (modelIndex <= 0)
				continue;

			model_t* model = gEngfuncs.hudGetModelByIndex(modelIndex);
			if (!model || !model->name)
				continue;

			const char* modelName = model->name;
			// Check that the model exists and is a studio model
			if (!model || model->type != mod_studio)
				continue;

			if (!ent->player && ent->curstate.messagenum != gEngfuncs.GetLocalPlayer()->curstate.messagenum)
				continue;
			int sequ = ent->curstate.sequence;
			bool isDeadScientistSequ = ((sequ >= 31 && sequ <= 42) || sequ == 84 || sequ == 115);
			bool isSittingScientistSequ = ((sequ >= 71 && sequ <= 75) || sequ == 78);
			bool DrawScientist = !(isDeadScientistSequ || isSittingScientistSequ);

			bool isDeadBarneySequ = ((sequ >= 25 && sequ <= 38) || (sequ >= 55 && sequ <= 59));
			bool isSittingBarneySequ = (sequ == 54);
			bool DrawBarney = !(isDeadBarneySequ || isSittingBarneySequ);
			// Only draw on ap models
			if (strstr(modelName, "models/ap-logo.mdl"))
				DrawBoundingBoxEntGL(ent, 1.0f, 1.0f, 0.0f, 1.0f);
			// and scientists
			if (strstr(modelName, "models/scientist.mdl") && DrawScientist)
				DrawBoundingBoxEntGL(ent, 1.0f, 0.0f, 0.0f, 1.0f);
			// and barney
			if (strstr(modelName, "models/barney.mdl") && DrawBarney)
				DrawBoundingBoxEntGL(ent, 1.0f, 0.0f, 0.0f, 1.0f);
			// DEBUG
			if (strstr(modelName, "models/scientist.mdl") && DrawScientist)
				printf("Drawing %s with sequ %i\n", modelName, sequ);
			if (strstr(modelName, "models/barney.mdl") && DrawBarney)
				printf("Drawing %s with sequ %i\n", modelName, sequ);
		}
	}


	if (g_pParticleMan)
		g_pParticleMan->Update();
}
