/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// g_ai.c

#include "g_local.h"

qBool FindTarget (edict_t *self);
extern cVar_t	*maxclients;

qBool ai_checkattack (edict_t *self, float dist);

qBool	enemy_vis;
qBool	enemy_infront;
int			enemy_range;
float		enemy_yaw;

//============================================================================


/*
=================
AI_SetSightClient

Called once each frame to set level.sight_client to the
player to be checked for in findtarget.

If all clients are either dead or in notarget, sight_client
will be null.

In coop games, sight_client will cycle between the clients.
=================
*/
void AI_SetSightClient (void)
{
	edict_t	*ent;
	int		start, check;

	if (level.sight_client == NULL)
		start = 1;
	else
		start = level.sight_client - g_edicts;

	check = start;
	while (1)
	{
		check++;
		if (check > game.maxclients)
			check = 1;
		ent = &g_edicts[check];
		if (ent->inUse
			&& ent->health > 0
			&& !(ent->flags & FL_NOTARGET) )
		{
			level.sight_client = ent;
			return;		// got one
		}
		if (check == start)
		{
			level.sight_client = NULL;
			return;		// nobody to see
		}
	}
}

//============================================================================

/*
=============
ai_move

Move the specified distance at current facing.
This replaces the QC functions: ai_forward, ai_back, ai_pain, and ai_painforward
==============
*/
void ai_move (edict_t *self, float dist)
{
	M_walkmove (self, self->s.angles[YAW], dist);
}


/*
=============
ai_stand

Used for standing around and looking for players
Distance is for slight position adjustments needed by the animations
==============
*/
void ai_stand (edict_t *self, float dist)
{
	vec3_t	v;

	if (dist)
		M_walkmove (self, self->s.angles[YAW], dist);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		if (self->enemy)
		{
			Vec3Subtract (self->enemy->s.origin, self->s.origin, v);
			self->ideal_yaw = VecToYaw(v);
			if (self->s.angles[YAW] != self->ideal_yaw && self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND)
			{
				self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
				self->monsterinfo.run (self);
			}
			M_ChangeYaw (self);
			ai_checkattack (self, 0);
		}
		else
			FindTarget (self);
		return;
	}

	if (FindTarget (self))
		return;
	
	if (level.time > self->monsterinfo.pausetime)
	{
		self->monsterinfo.walk (self);
		return;
	}

	if (!(self->spawnflags & 1) && (self->monsterinfo.idle) && (level.time > self->monsterinfo.idle_time))
	{
		if (self->monsterinfo.idle_time)
		{
			self->monsterinfo.idle (self);
			self->monsterinfo.idle_time = level.time + 15 + random() * 15;
		}
		else
		{
			self->monsterinfo.idle_time = level.time + random() * 15;
		}
	}
}


/*
=============
ai_walk

The monster is walking it's beat
=============
*/
void ai_walk (edict_t *self, float dist)
{
	M_MoveToGoal (self, dist);

	// check for noticing a player
	if (FindTarget (self))
		return;

	if ((self->monsterinfo.search) && (level.time > self->monsterinfo.idle_time))
	{
		if (self->monsterinfo.idle_time)
		{
			self->monsterinfo.search (self);
			self->monsterinfo.idle_time = level.time + 15 + random() * 15;
		}
		else
		{
			self->monsterinfo.idle_time = level.time + random() * 15;
		}
	}
}


/*
=============
ai_charge

Turns towards target and advances
Use this call with a distnace of 0 to replace ai_face
==============
*/
void ai_charge (edict_t *self, float dist)
{
	vec3_t	v;

	Vec3Subtract (self->enemy->s.origin, self->s.origin, v);
	self->ideal_yaw = VecToYaw(v);
	M_ChangeYaw (self);

	if (dist)
		M_walkmove (self, self->s.angles[YAW], dist);
}


/*
=============
ai_turn

don't move, but turn towards ideal_yaw
Distance is for slight position adjustments needed by the animations
=============
*/
void ai_turn (edict_t *self, float dist)
{
	if (dist)
		M_walkmove (self, self->s.angles[YAW], dist);

	if (FindTarget (self))
		return;
	
	M_ChangeYaw (self);
}


/*

.enemy
Will be world if not currently angry at anyone.

.movetarget
The next path spot to walk toward.  If .enemy, ignore .movetarget.
When an enemy is killed, the monster will try to return to it's path.

.hunt_time
Set to time + something when the player is in sight, but movement straight for
him is blocked.  This causes the monster to use wall following code for
movement direction instead of sighting on the player.

.ideal_yaw
A yaw angle of the intended direction, which will be turned towards at up
to 45 deg / state.  If the enemy is in view and hunt_time is not active,
this will be the exact line towards the enemy.

.pausetime
A monster will leave it's stand state and head towards it's .movetarget when
time > .pausetime.

walkmove(angle, speed) primitive is all or nothing
*/

/*
=============
range

returns the range catagorization of an entity reletive to self
0	melee range, will become hostile even if back is turned
1	visibility and infront, or visibility and show hostile
2	infront and show hostile
3	only triggered by damage
=============
*/
int range (edict_t *self, edict_t *other)
{
	vec3_t	v;
	float	len;

	Vec3Subtract (self->s.origin, other->s.origin, v);
	len = Vec3Length (v);
	if (len < MELEE_DISTANCE)
		return RANGE_MELEE;
	if (len < 500)
		return RANGE_NEAR;
	if (len < 1000)
		return RANGE_MID;
	return RANGE_FAR;
}

/*
=============
visible

returns 1 if the entity is visible to self, even if not infront ()
=============
*/
qBool visible (edict_t *self, edict_t *other)
{
	vec3_t	spot1;
	vec3_t	spot2;
	trace_t	trace;

	Vec3Copy (self->s.origin, spot1);
	spot1[2] += self->viewheight;
	Vec3Copy (other->s.origin, spot2);
	spot2[2] += other->viewheight;
	trace = gi.trace (spot1, vec3Origin, vec3Origin, spot2, self, MASK_OPAQUE);
	
	if (trace.fraction == 1.0)
		return qTrue;
	return qFalse;
}


/*
=============
infront

returns 1 if the entity is in front (in sight) of self
=============
*/
qBool infront (edict_t *self, edict_t *other)
{
	vec3_t	vec;
	float	dot;
	vec3_t	forward;
	
	Angles_Vectors (self->s.angles, forward, NULL, NULL);
	Vec3Subtract (other->s.origin, self->s.origin, vec);
	VectorNormalizef (vec, vec);
	dot = DotProduct (vec, forward);
	
	if (dot > 0.3)
		return qTrue;
	return qFalse;
}


//============================================================================

void HuntTarget (edict_t *self)
{
	vec3_t	vec;

	self->goalentity = self->enemy;
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.stand (self);
	else
		self->monsterinfo.run (self);
	Vec3Subtract (self->enemy->s.origin, self->s.origin, vec);
	self->ideal_yaw = VecToYaw(vec);
	// wait a while before first attack
	if (!(self->monsterinfo.aiflags & AI_STAND_GROUND))
		AttackFinished (self, 1);
}

void FoundTarget (edict_t *self)
{
	// let other monsters see this monster for a while
	if (self->enemy->client)
	{
		level.sight_entity = self;
		level.sight_entity_framenum = level.framenum;
		level.sight_entity->light_level = 128;
	}

	self->show_hostile = level.time + 1;		// wake up other monsters

	Vec3Copy(self->enemy->s.origin, self->monsterinfo.last_sighting);
	self->monsterinfo.trail_time = level.time;

	if (!self->combattarget)
	{
		HuntTarget (self);
		return;
	}

	self->goalentity = self->movetarget = G_PickTarget(self->combattarget);
	if (!self->movetarget)
	{
		self->goalentity = self->movetarget = self->enemy;
		HuntTarget (self);
		gi.dprintf("%s at %s, combattarget %s not found\n", self->classname, vtos(self->s.origin), self->combattarget);
		return;
	}

	// clear out our combattarget, these are a one shot deal
	self->combattarget = NULL;
	self->monsterinfo.aiflags |= AI_COMBAT_POINT;

	// clear the targetname, that point is ours!
	self->movetarget->targetname = NULL;
	self->monsterinfo.pausetime = 0;

	// run for it
	self->monsterinfo.run (self);
}


/*
===========
FindTarget

Self is currently not attacking anything, so try to find a target

Returns qTrue if an enemy was sighted

When a player fires a missile, the point of impact becomes a fakeplayer so
that monsters that see the impact will respond as if they had seen the
player.

To avoid spending too much time, only a single client (or fakeclient) is
checked each frame.  This means multi player games will have slightly
slower noticing monsters.
============
*/
qBool FindTarget (edict_t *self)
{
	edict_t		*client;
	qBool	heardit;
	int			r;

	if (self->monsterinfo.aiflags & AI_GOOD_GUY)
	{
		if (self->goalentity && self->goalentity->inUse && self->goalentity->classname)
		{
			if (strcmp(self->goalentity->classname, "target_actor") == 0)
				return qFalse;
		}

		//FIXME look for monsters?
		return qFalse;
	}

	// if we're going to a combat point, just proceed
	if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
		return qFalse;

// if the first spawnflag bit is set, the monster will only wake up on
// really seeing the player, not another monster getting angry or hearing
// something

// revised behavior so they will wake up if they "see" a player make a noise
// but not weapon impact/explosion noises

	heardit = qFalse;
	if ((level.sight_entity_framenum >= (level.framenum - 1)) && !(self->spawnflags & 1) )
	{
		client = level.sight_entity;
		if (client->enemy == self->enemy)
		{
			return qFalse;
		}
	}
	else if (level.sound_entity_framenum >= (level.framenum - 1))
	{
		client = level.sound_entity;
		heardit = qTrue;
	}
	else if (!(self->enemy) && (level.sound2_entity_framenum >= (level.framenum - 1)) && !(self->spawnflags & 1) )
	{
		client = level.sound2_entity;
		heardit = qTrue;
	}
	else
	{
		client = level.sight_client;
		if (!client)
			return qFalse;	// no clients to get mad at
	}

	// if the entity went away, forget it
	if (!client->inUse)
		return qFalse;

	if (client == self->enemy)
		return qTrue;	// JDC false;

	if (client->client)
	{
		if (client->flags & FL_NOTARGET)
			return qFalse;
	}
	else if (client->svFlags & SVF_MONSTER)
	{
		if (!client->enemy)
			return qFalse;
		if (client->enemy->flags & FL_NOTARGET)
			return qFalse;
	}
	else if (heardit)
	{
		if (client->owner->flags & FL_NOTARGET)
			return qFalse;
	}
	else
		return qFalse;

	if (!heardit)
	{
		r = range (self, client);

		if (r == RANGE_FAR)
			return qFalse;

// this is where we would check invisibility

		// is client in an spot too dark to be seen?
		if (client->light_level <= 5)
			return qFalse;

		if (!visible (self, client))
		{
			return qFalse;
		}

		if (r == RANGE_NEAR)
		{
			if (client->show_hostile < level.time && !infront (self, client))
			{
				return qFalse;
			}
		}
		else if (r == RANGE_MID)
		{
			if (!infront (self, client))
			{
				return qFalse;
			}
		}

		self->enemy = client;

		if (strcmp(self->enemy->classname, "player_noise") != 0)
		{
			self->monsterinfo.aiflags &= ~AI_SOUND_TARGET;

			if (!self->enemy->client)
			{
				self->enemy = self->enemy->enemy;
				if (!self->enemy->client)
				{
					self->enemy = NULL;
					return qFalse;
				}
			}
		}
	}
	else	// heardit
	{
		vec3_t	temp;

		if (self->spawnflags & 1)
		{
			if (!visible (self, client))
				return qFalse;
		}
		else
		{
			if (!gi.inPHS(self->s.origin, client->s.origin))
				return qFalse;
		}

		Vec3Subtract (client->s.origin, self->s.origin, temp);

		if (Vec3Length(temp) > 1000)	// too far to hear
		{
			return qFalse;
		}

		// check area portals - if they are different and not connected then we can't hear it
		if (client->areaNum != self->areaNum)
			if (!gi.AreasConnected(self->areaNum, client->areaNum))
				return qFalse;

		self->ideal_yaw = VecToYaw(temp);
		M_ChangeYaw (self);

		// hunt the sound for a bit; hopefully find the real player
		self->monsterinfo.aiflags |= AI_SOUND_TARGET;
		self->enemy = client;
	}

//
// got one
//
	FoundTarget (self);

	if (!(self->monsterinfo.aiflags & AI_SOUND_TARGET) && (self->monsterinfo.sight))
		self->monsterinfo.sight (self, self->enemy);

	return qTrue;
}


//=============================================================================

/*
============
FacingIdeal

============
*/
qBool FacingIdeal(edict_t *self)
{
	float	delta;

	delta = AngleModf (self->s.angles[YAW] - self->ideal_yaw);
	if (delta > 45 && delta < 315)
		return qFalse;
	return qTrue;
}


//=============================================================================

qBool M_CheckAttack (edict_t *self)
{
	vec3_t	spot1, spot2;
	float	chance;
	trace_t	tr;

	if (self->enemy->health > 0)
	{
	// see if any entities are in the way of the shot
		Vec3Copy (self->s.origin, spot1);
		spot1[2] += self->viewheight;
		Vec3Copy (self->enemy->s.origin, spot2);
		spot2[2] += self->enemy->viewheight;

		tr = gi.trace (spot1, NULL, NULL, spot2, self, CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_SLIME|CONTENTS_LAVA|CONTENTS_WINDOW);

		// do we have a clear shot?
		if (tr.ent != self->enemy)
			return qFalse;
	}
	
	// melee attack
	if (enemy_range == RANGE_MELEE)
	{
		// don't always melee in easy mode
		if (skill->floatVal == 0 && (rand()&3) )
			return qFalse;
		if (self->monsterinfo.melee)
			self->monsterinfo.attack_state = AS_MELEE;
		else
			self->monsterinfo.attack_state = AS_MISSILE;
		return qTrue;
	}
	
// missile attack
	if (!self->monsterinfo.attack)
		return qFalse;
		
	if (level.time < self->monsterinfo.attack_finished)
		return qFalse;
		
	if (enemy_range == RANGE_FAR)
		return qFalse;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		chance = 0.4f;
	}
	else if (enemy_range == RANGE_MELEE)
	{
		chance = 0.2f;
	}
	else if (enemy_range == RANGE_NEAR)
	{
		chance = 0.1f;
	}
	else if (enemy_range == RANGE_MID)
	{
		chance = 0.02f;
	}
	else
	{
		return qFalse;
	}

	if (skill->floatVal == 0)
		chance *= 0.5;
	else if (skill->floatVal >= 2)
		chance *= 2;

	if (random () < chance)
	{
		self->monsterinfo.attack_state = AS_MISSILE;
		self->monsterinfo.attack_finished = level.time + 2*random();
		return qTrue;
	}

	if (self->flags & FL_FLY)
	{
		if (random() < 0.3)
			self->monsterinfo.attack_state = AS_SLIDING;
		else
			self->monsterinfo.attack_state = AS_STRAIGHT;
	}

	return qFalse;
}


/*
=============
ai_run_melee

Turn and close until within an angle to launch a melee attack
=============
*/
void ai_run_melee(edict_t *self)
{
	self->ideal_yaw = enemy_yaw;
	M_ChangeYaw (self);

	if (FacingIdeal(self))
	{
		self->monsterinfo.melee (self);
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}
}


/*
=============
ai_run_missile

Turn in place until within an angle to launch a missile attack
=============
*/
void ai_run_missile(edict_t *self)
{
	self->ideal_yaw = enemy_yaw;
	M_ChangeYaw (self);

	if (FacingIdeal(self))
	{
		self->monsterinfo.attack (self);
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}
};


/*
=============
ai_run_slide

Strafe sideways, but stay at aproximately the same range
=============
*/
void ai_run_slide(edict_t *self, float distance)
{
	float	ofs;
	
	self->ideal_yaw = enemy_yaw;
	M_ChangeYaw (self);

	if (self->monsterinfo.lefty)
		ofs = 90;
	else
		ofs = -90;
	
	if (M_walkmove (self, self->ideal_yaw + ofs, distance))
		return;
		
	self->monsterinfo.lefty = 1 - self->monsterinfo.lefty;
	M_walkmove (self, self->ideal_yaw - ofs, distance);
}


/*
=============
ai_checkattack

Decides if we're going to attack or do something else
used by ai_run and ai_stand
=============
*/
qBool ai_checkattack (edict_t *self, float dist)
{
	vec3_t		temp;
	qBool	hesDeadJim;

// this causes monsters to run blindly to the combat point w/o firing
	if (self->goalentity)
	{
		if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
			return qFalse;

		if (self->monsterinfo.aiflags & AI_SOUND_TARGET)
		{
			if ((level.time - self->enemy->teleport_time) > 5.0)
			{
				if (self->goalentity == self->enemy)
					if (self->movetarget)
						self->goalentity = self->movetarget;
					else
						self->goalentity = NULL;
				self->monsterinfo.aiflags &= ~AI_SOUND_TARGET;
				if (self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND)
					self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
			}
			else
			{
				self->show_hostile = level.time + 1;
				return qFalse;
			}
		}
	}

	enemy_vis = qFalse;

// see if the enemy is dead
	hesDeadJim = qFalse;
	if ((!self->enemy) || (!self->enemy->inUse))
	{
		hesDeadJim = qTrue;
	}
	else if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		if (self->enemy->health > 0)
		{
			hesDeadJim = qTrue;
			self->monsterinfo.aiflags &= ~AI_MEDIC;
		}
	}
	else
	{
		if (self->monsterinfo.aiflags & AI_BRUTAL)
		{
			if (self->enemy->health <= -80)
				hesDeadJim = qTrue;
		}
		else
		{
			if (self->enemy->health <= 0)
				hesDeadJim = qTrue;
		}
	}

	if (hesDeadJim)
	{
		self->enemy = NULL;
	// FIXME: look all around for other targets
		if (self->oldenemy && self->oldenemy->health > 0)
		{
			self->enemy = self->oldenemy;
			self->oldenemy = NULL;
			HuntTarget (self);
		}
		else
		{
			if (self->movetarget)
			{
				self->goalentity = self->movetarget;
				self->monsterinfo.walk (self);
			}
			else
			{
				// we need the pausetime otherwise the stand code
				// will just revert to walking with no target and
				// the monsters will wonder around aimlessly trying
				// to hunt the world entity
				self->monsterinfo.pausetime = level.time + 100000000;
				self->monsterinfo.stand (self);
			}
			return qTrue;
		}
	}

	self->show_hostile = level.time + 1;		// wake up other monsters

// check knowledge of enemy
	enemy_vis = visible(self, self->enemy);
	if (enemy_vis)
	{
		self->monsterinfo.search_time = level.time + 5;
		Vec3Copy (self->enemy->s.origin, self->monsterinfo.last_sighting);
	}

// look for other coop players here
//	if (coop && self->monsterinfo.search_time < level.time)
//	{
//		if (FindTarget (self))
//			return qTrue;
//	}

	enemy_infront = infront(self, self->enemy);
	enemy_range = range(self, self->enemy);
	Vec3Subtract (self->enemy->s.origin, self->s.origin, temp);
	enemy_yaw = VecToYaw(temp);


	// JDC self->ideal_yaw = enemy_yaw;

	if (self->monsterinfo.attack_state == AS_MISSILE)
	{
		ai_run_missile (self);
		return qTrue;
	}
	if (self->monsterinfo.attack_state == AS_MELEE)
	{
		ai_run_melee (self);
		return qTrue;
	}

	// if enemy is not currently visible, we will never attack
	if (!enemy_vis)
		return qFalse;

	return self->monsterinfo.checkattack (self);
}


/*
=============
ai_run

The monster has an enemy it is trying to kill
=============
*/
void ai_run (edict_t *self, float dist)
{
	vec3_t		v;
	edict_t		*tempgoal;
	edict_t		*save;
	qBool	new;
	edict_t		*marker;
	float		d1, d2;
	trace_t		tr;
	vec3_t		v_forward, v_right;
	float		left, center, right;
	vec3_t		left_target, right_target;

	// if we're going to a combat point, just proceed
	if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
	{
		M_MoveToGoal (self, dist);
		return;
	}

	if (self->monsterinfo.aiflags & AI_SOUND_TARGET)
	{
		Vec3Subtract (self->s.origin, self->enemy->s.origin, v);
		if (Vec3Length(v) < 64)
		{
			self->monsterinfo.aiflags |= (AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
			self->monsterinfo.stand (self);
			return;
		}

		M_MoveToGoal (self, dist);

		if (!FindTarget (self))
			return;
	}

	if (ai_checkattack (self, dist))
		return;

	if (self->monsterinfo.attack_state == AS_SLIDING)
	{
		ai_run_slide (self, dist);
		return;
	}

	if (enemy_vis)
	{
//		if (self.aiflags & AI_LOST_SIGHT)
//			dprint("regained sight\n");
		M_MoveToGoal (self, dist);
		self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
		Vec3Copy (self->enemy->s.origin, self->monsterinfo.last_sighting);
		self->monsterinfo.trail_time = level.time;
		return;
	}

	// coop will change to another enemy if visible
	if (coop->floatVal)
	{	// FIXME: insane guys get mad with this, which causes crashes!
		if (FindTarget (self))
			return;
	}

	if ((self->monsterinfo.search_time) && (level.time > (self->monsterinfo.search_time + 20)))
	{
		M_MoveToGoal (self, dist);
		self->monsterinfo.search_time = 0;
//		dprint("search timeout\n");
		return;
	}

	save = self->goalentity;
	tempgoal = G_Spawn();
	self->goalentity = tempgoal;

	new = qFalse;

	if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT))
	{
		// just lost sight of the player, decide where to go first
//		dprint("lost sight of player, last seen at "); dprint(vtos(self.last_sighting)); dprint("\n");
		self->monsterinfo.aiflags |= (AI_LOST_SIGHT | AI_PURSUIT_LAST_SEEN);
		self->monsterinfo.aiflags &= ~(AI_PURSUE_NEXT | AI_PURSUE_TEMP);
		new = qTrue;
	}

	if (self->monsterinfo.aiflags & AI_PURSUE_NEXT)
	{
		self->monsterinfo.aiflags &= ~AI_PURSUE_NEXT;
//		dprint("reached current goal: "); dprint(vtos(self.origin)); dprint(" "); dprint(vtos(self.last_sighting)); dprint(" "); dprint(ftos(vlen(self.origin - self.last_sighting))); dprint("\n");

		// give ourself more time since we got this far
		self->monsterinfo.search_time = level.time + 5;

		if (self->monsterinfo.aiflags & AI_PURSUE_TEMP)
		{
//			dprint("was temp goal; retrying original\n");
			self->monsterinfo.aiflags &= ~AI_PURSUE_TEMP;
			marker = NULL;
			Vec3Copy (self->monsterinfo.saved_goal, self->monsterinfo.last_sighting);
			new = qTrue;
		}
		else if (self->monsterinfo.aiflags & AI_PURSUIT_LAST_SEEN)
		{
			self->monsterinfo.aiflags &= ~AI_PURSUIT_LAST_SEEN;
			marker = PlayerTrail_PickFirst (self);
		}
		else
		{
			marker = PlayerTrail_PickNext (self);
		}

		if (marker)
		{
			Vec3Copy (marker->s.origin, self->monsterinfo.last_sighting);
			self->monsterinfo.trail_time = marker->timestamp;
			self->s.angles[YAW] = self->ideal_yaw = marker->s.angles[YAW];
//			dprint("heading is "); dprint(ftos(self.ideal_yaw)); dprint("\n");

//			debug_drawline(self.origin, self.last_sighting, 52);
			new = qTrue;
		}
	}

	Vec3Subtract (self->s.origin, self->monsterinfo.last_sighting, v);
	d1 = Vec3Length(v);
	if (d1 <= dist)
	{
		self->monsterinfo.aiflags |= AI_PURSUE_NEXT;
		dist = d1;
	}

	Vec3Copy (self->monsterinfo.last_sighting, self->goalentity->s.origin);

	if (new)
	{
//		gi.dprintf("checking for course correction\n");

		tr = gi.trace(self->s.origin, self->mins, self->maxs, self->monsterinfo.last_sighting, self, MASK_PLAYERSOLID);
		if (tr.fraction < 1)
		{
			Vec3Subtract (self->goalentity->s.origin, self->s.origin, v);
			d1 = Vec3Length(v);
			center = tr.fraction;
			d2 = d1 * ((center+1)/2);
			self->s.angles[YAW] = self->ideal_yaw = VecToYaw(v);
			Angles_Vectors(self->s.angles, v_forward, v_right, NULL);

			Vec3Set (v, d2, -16, 0);
			G_ProjectSource (self->s.origin, v, v_forward, v_right, left_target);
			tr = gi.trace(self->s.origin, self->mins, self->maxs, left_target, self, MASK_PLAYERSOLID);
			left = tr.fraction;

			Vec3Set (v, d2, 16, 0);
			G_ProjectSource (self->s.origin, v, v_forward, v_right, right_target);
			tr = gi.trace(self->s.origin, self->mins, self->maxs, right_target, self, MASK_PLAYERSOLID);
			right = tr.fraction;

			center = (d1*center)/d2;
			if (left >= center && left > right)
			{
				if (left < 1)
				{
					Vec3Set (v, d2 * left * 0.5, -16, 0);
					G_ProjectSource (self->s.origin, v, v_forward, v_right, left_target);
//					gi.dprintf("incomplete path, go part way and adjust again\n");
				}
				Vec3Copy (self->monsterinfo.last_sighting, self->monsterinfo.saved_goal);
				self->monsterinfo.aiflags |= AI_PURSUE_TEMP;
				Vec3Copy (left_target, self->goalentity->s.origin);
				Vec3Copy (left_target, self->monsterinfo.last_sighting);
				Vec3Subtract (self->goalentity->s.origin, self->s.origin, v);
				self->s.angles[YAW] = self->ideal_yaw = VecToYaw(v);
//				gi.dprintf("adjusted left\n");
//				debug_drawline(self.origin, self.last_sighting, 152);
			}
			else if (right >= center && right > left)
			{
				if (right < 1) {
					Vec3Set (v, d2 * right * 0.5, 16, 0);
					G_ProjectSource (self->s.origin, v, v_forward, v_right, right_target);
//					gi.dprintf("incomplete path, go part way and adjust again\n");
				}
				Vec3Copy (self->monsterinfo.last_sighting, self->monsterinfo.saved_goal);
				self->monsterinfo.aiflags |= AI_PURSUE_TEMP;
				Vec3Copy (right_target, self->goalentity->s.origin);
				Vec3Copy (right_target, self->monsterinfo.last_sighting);
				Vec3Subtract (self->goalentity->s.origin, self->s.origin, v);
				self->s.angles[YAW] = self->ideal_yaw = VecToYaw(v);
//				gi.dprintf("adjusted right\n");
//				debug_drawline(self.origin, self.last_sighting, 152);
			}
		}
//		else gi.dprintf("course was fine\n");
	}

	M_MoveToGoal (self, dist);

	G_FreeEdict(tempgoal);

	if (self)
		self->goalentity = save;
}