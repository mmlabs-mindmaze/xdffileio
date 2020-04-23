/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "xdfevent.h"



static
int find_entry(struct eventtable* table, int code, const char* s)
{
	unsigned int i;

	for (i=0; i<table->nentry; i++) {
		if ( (table->entry[i].code == code)
		  && ((s == NULL) == (table->entry[i].label == NULL))
		  && (s != NULL && strcmp(table->entry[i].label, s) == 0)) 
			return i;
	}
	return -1;
}


/**
 * add_event_entry() - add an event entry in the table of events
 * @table: table of events
 * @code: code of the event
 * @label: label of the event
 *
 * Return: the index of the table in which the event entry is added in case of
 *         success, -1 otherwise
 */
LOCAL_FN
int add_event_entry(struct eventtable* table, 
                          int code, const char* label)
{
	struct evententry *entry;
	unsigned int roundup, nentry = table->nentry;
	int evttype;
	char* elabel = NULL;

	evttype = find_entry(table, code, label);
	
	if (evttype < 0) {
		// Resize the event type table
		roundup = (((table->nentry+1)/64)+1)*64;
		entry = realloc(table->entry, roundup*sizeof(*entry));
		if (entry == NULL)
			return -1;
		table->entry = entry;

		// Allocate copy label if necessary
		if (label != NULL) {
			elabel = malloc(strlen(label)+1);
			if (elabel == NULL)
				return -1;
			strcpy(elabel, label);
		}

		evttype = nentry;
		entry[evttype].code = code;
		entry[evttype].label = elabel;
		table->nentry++;
	}

	return evttype;
}

/**
 * get_event_entry() - gets the event entry at a given index of the table of
 *                     events
 * @table: table of events
 * @ind: index of the table at which the event entry is retrieved
 * @code: integer filled with the code of the event
 * @label: string filled with the label of the event
 *
 * Return: 0
 */
LOCAL_FN
int get_event_entry(struct eventtable* table, unsigned int ind,
                              int *code, const char** label)
{
	*code = table->entry[ind].code;	
	*label = table->entry[ind].label;
	return 0;
}


/**
 * add_event() - add an event in the table of events
 * @table: table of events
 * @evt: pointer to an event
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN
int add_event(struct eventtable* table, struct xdfevent* evt)
{
	struct eventbatch* batch = table->last;
	int index = table->nevent % N_EVT_BATCH;

	// Happens a batch if full
	if (index == 0) {
		batch = malloc(sizeof(*batch));
		if (batch == NULL)
			return -1;

		batch->next = NULL;
		if (table->last)
			table->last->next = batch;
		else
			table->first = batch;
		table->last = batch;
	}

	memcpy(batch->evt + index, evt, sizeof(*evt));
	table->nevent++;

	return 0;
}


/**
 * create_event_table() - creates a table of events
 *
 * Return: the allocated table in case of success, NULL otherwise
 */
LOCAL_FN
struct eventtable* create_event_table(void)
{
	struct eventtable* table = NULL;
	
	table = malloc(sizeof(*table));
	if (table == NULL)
		return NULL;
	
	table->nentry = 0;
	table->entry = NULL;
	table->nevent = 0;
	table->first = table->last = NULL;

	return table;
}


/**
 * destroy_event_table() - destroys a table of events
 * @table: table of events to destroy
 */
LOCAL_FN 
void destroy_event_table(struct eventtable* table)
{
	struct eventbatch *curr_batch, *next_batch;
	unsigned int i;

	if (table == NULL)
		return;
	
	next_batch = table->first;
	while (next_batch) {
		curr_batch = next_batch;
		next_batch = next_batch->next;
		free(curr_batch);
	}

	for (i=0; i<table->nentry; i++)
		free(table->entry[i].label);
	free(table->entry);

	free(table);
}


/**
 * get_event() - gets the event at a given index of the table of events
 * @table: table of events
 * @index:  index of the table at which the event is retrieved
 *
 * Return: the event at the index @index of the table of events
 */
LOCAL_FN
struct xdfevent* get_event(struct eventtable* table, unsigned int index)
{
	struct eventbatch* batch = table->first;
	
	while (index >= N_EVT_BATCH) {
		batch = batch->next;
		index -= N_EVT_BATCH;
	}

	return batch->evt + index;
}
