/*
 * Copyright (C) 2019 MindMaze
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef REFSIGNAL_H
#define REFSIGNAL_H

#include <math.h>
#include <stdio.h>

#define FS 256
#define MAXFSIZE 1000
#define NS 2000

#define NEVTTYPE 5


struct event {
	int type;
	double onset;
	double dur;
	int code;
};


static inline
float get_signal(int i)
{
	return cosf((2.0*M_PI*i)/FS);
}


static inline
int sample_has_event(int i)
{
	return ((i % 12 == 5) || (i % 63 == 3));
}


static inline
int get_event_code(int type)
{
	return (0x12 << type);
}


static inline
void get_event(int i, struct event* evt)
{
	evt->type = (7 * i) % NEVTTYPE;
	evt->dur = -1.;
	evt->onset = (double)(7*i + ((13*i)%3))/FS;
}

#endif
