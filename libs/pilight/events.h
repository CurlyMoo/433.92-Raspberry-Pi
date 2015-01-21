/*
	Copyright (C) 2013 CurlyMo

	This file is part of pilight.

	pilight is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	pilight is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with pilight. If not, see	<http://www.gnu.org/licenses/>
*/

#ifndef _EVENTS_H_
#define _EVENTS_H_

#include "rules.h"

int event_parse_rule(char *rule, struct rules_t *obj, int depth, unsigned int nr, int validate);
void *events_clientize(void *param);
int events_gc(void);
void *events_loop(void *param);
int events_running(void);

#endif
