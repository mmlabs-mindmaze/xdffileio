/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef XDFEVENT_H
#define XDFEVENT_H

#define N_EVT_BATCH	50

struct xdfevent {
	double onset, duration;
	int evttype;
};

struct eventbatch {
	struct xdfevent evt[N_EVT_BATCH];
	struct eventbatch* next;
};

struct evententry {
	int code;
	char* label;
};

struct eventtable {
	unsigned int nentry;
	struct evententry *entry;
	unsigned int nevent;
	struct eventbatch* first;
	struct eventbatch* last;
};

LOCAL_FN struct eventtable* create_event_table(void);
LOCAL_FN void destroy_event_table(struct eventtable* table);
LOCAL_FN int add_event(struct eventtable* table, struct xdfevent* evt);
LOCAL_FN struct xdfevent* get_event(struct eventtable* table,
                                     unsigned int index);
LOCAL_FN int add_event_entry(struct eventtable* table, int code,
                                                   const char* label);
LOCAL_FN int get_event_entry(struct eventtable* table, unsigned int ind,
                              int *code, const char** label);

#endif /* XDFEVENT_H */

