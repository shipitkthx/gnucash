/********************************************************************\
 * reconcile-list.c -- A list of accounts to be reconciled for      *
 *                     GnuCash.                                     *
 * Copyright (C) 1998,1999 Jeremy Collins	                    *
 * Copyright (C) 1998,1999 Linas Vepstas                            *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, write to the Free Software      *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.        *
\********************************************************************/

#include <gnome.h>

#include "config.h"

#include "top-level.h"
#include "gnucash.h"
#include "messages.h"
#include "reconcile-listP.h"
#include "date.h"
#include "util.h"


static GtkCListClass *parent_class = NULL;
static guint reconcile_list_signals[LAST_SIGNAL] = {0};


GtkType
gnc_reconcile_list_get_type()
{
  static GtkType gnc_reconcile_list_type = 0;

  if (!gnc_reconcile_list_type)
  {
    static const GtkTypeInfo gnc_reconcile_list_info =
    {
      "GNCReconcileList",
      sizeof (GNCReconcileList),
      sizeof (GNCReconcileListClass),
      (GtkClassInitFunc) gnc_reconcile_list_class_init,
      (GtkObjectInitFunc) gnc_reconcile_list_init,
      /* reserved_1 */ NULL,
      /* reserved_2 */ NULL,
      (GtkClassInitFunc) NULL
    };

    gnc_reconcile_list_type = gtk_type_unique(GTK_TYPE_CLIST,
					      &gnc_reconcile_list_info);
  }

  return gnc_reconcile_list_type;
}


/********************************************************************\
 * gnc_reconcile_list_new                                           *
 *   creates the account tree                                       *
 *                                                                  *
 * Args: account - the account to use in filling up the splits.     *
 *       type    - the type of list, RECLIST_DEBIT or RECLIST_CREDIT*
 * Returns: the account tree widget, or NULL if there was a problem.*
\********************************************************************/
GtkWidget *
gnc_reconcile_list_new(Account *account, GNCReconcileListType type)
{
  GNCReconcileList *list;

  assert(account != NULL);
  assert((type == RECLIST_DEBIT) || (type == RECLIST_CREDIT));

  list = GNC_RECONCILE_LIST(gtk_type_new(gnc_reconcile_list_get_type()));

  list->account = account;
  list->list_type = type;

  return GTK_WIDGET(list);
}

static void
gnc_reconcile_list_init(GNCReconcileList *list)
{
  GtkCList *clist = GTK_CLIST(list);
  static gchar * titles[] =
    {
      DATE_STR,
      NUM_STR,
      DESC_STR,
      AMT_STR,
      "?",
      NULL
    };

  list->num_splits = 0;
  list->num_columns = 0;
  list->reconciled = NULL;
  list->current_row = -1;

  while (titles[list->num_columns] != NULL)
    list->num_columns++;

  gtk_clist_construct(clist, list->num_columns, titles);
  gtk_clist_set_shadow_type (clist, GTK_SHADOW_IN);
  gtk_clist_set_column_justification(clist, 3, GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_justification(clist, 4, GTK_JUSTIFY_CENTER);
  gtk_clist_column_titles_passive(clist);

  {
    GtkStyle *st = gtk_widget_get_style(GTK_WIDGET(list));
    GdkFont *font = NULL;
    gint width;
    gint i;

    if (st != NULL)
      font = st->font;

    if (font != NULL)
      for (i = 0; i < list->num_columns; i++)
      {
	width = gdk_string_width(font, titles[i]);
	gtk_clist_set_column_min_width(GTK_CLIST(list), i, width + 5);
	if (i == 4)
	  gtk_clist_set_column_max_width(GTK_CLIST(list), i, width + 5);
      }
  }

#if !USE_NO_COLOR
  {
    GdkColormap *cm = gtk_widget_get_colormap(GTK_WIDGET(list));
    GtkStyle *style = gtk_widget_get_style(GTK_WIDGET(list));

    list->reconciled_style = gtk_style_copy(style);
    style = list->reconciled_style;

    /* A dark green */
    style->fg[GTK_STATE_NORMAL].red   = 0;
    style->fg[GTK_STATE_NORMAL].green = 40000;
    style->fg[GTK_STATE_NORMAL].blue  = 0;

    gdk_colormap_alloc_color(cm, &style->fg[GTK_STATE_NORMAL], FALSE, TRUE);

    style->fg[GTK_STATE_SELECTED] = style->fg[GTK_STATE_NORMAL];
  }
#endif
}

static void
gnc_reconcile_list_class_init(GNCReconcileListClass *klass)
{
  GtkObjectClass    *object_class;
  GtkWidgetClass    *widget_class;
  GtkContainerClass *container_class;
  GtkCListClass     *clist_class;

  object_class =    (GtkObjectClass*) klass;
  widget_class =    (GtkWidgetClass*) klass;
  container_class = (GtkContainerClass*) klass;
  clist_class =     (GtkCListClass*) klass;

  parent_class = gtk_type_class(GTK_TYPE_CLIST);

  reconcile_list_signals[TOGGLE_RECONCILED] =
    gtk_signal_new("toggle_reconciled",
		   GTK_RUN_FIRST,
		   object_class->type,
		   GTK_SIGNAL_OFFSET(GNCReconcileListClass,
				     toggle_reconciled),
		   gtk_marshal_NONE__POINTER,
		   GTK_TYPE_NONE, 1,
		   GTK_TYPE_POINTER);

  gtk_object_class_add_signals(object_class,
			       reconcile_list_signals,
			       LAST_SIGNAL);

  object_class->destroy = gnc_reconcile_list_destroy;

  clist_class->select_row = gnc_reconcile_list_select_row;
  clist_class->unselect_row = gnc_reconcile_list_unselect_row;

  klass->toggle_reconciled = NULL;
}

static void
gnc_reconcile_list_toggle(GNCReconcileList *list)
{
  Split *split;
  char recn_str[2];
  char recn;
  gint row;

  assert(GTK_IS_GNC_RECONCILE_LIST(list));
  assert(list->reconciled != NULL);

  row = list->current_row;
  list->reconciled[row] = !list->reconciled[row];

  split = gtk_clist_get_row_data(GTK_CLIST(list), row);

  recn = xaccSplitGetReconcile(split);
  g_snprintf(recn_str, 2, "%c", list->reconciled[row] ? YREC : recn);
  gtk_clist_set_text(GTK_CLIST(list), row, 4, recn_str);

#if !USE_NO_COLOR
  if (list->reconciled[row])
    gtk_clist_set_cell_style(GTK_CLIST(list), row, 4, list->reconciled_style);
  else
    gtk_clist_set_cell_style(GTK_CLIST(list), row, 4,
			     gtk_widget_get_style(GTK_WIDGET(list)));
#endif

  gtk_signal_emit(GTK_OBJECT(list),
		  reconcile_list_signals[TOGGLE_RECONCILED],
		  split);
}

static void
gnc_reconcile_list_select_row(GtkCList *clist, gint row, gint column,
			      GdkEvent *event)
{
  GNCReconcileList *list = GNC_RECONCILE_LIST(clist);

  list->current_row = row;
  gnc_reconcile_list_toggle(list);

  GTK_CLIST_CLASS(parent_class)->select_row(clist, row, column, event);
}

static void
gnc_reconcile_list_unselect_row(GtkCList *clist, gint row, gint column,
				GdkEvent *event)
{
  GNCReconcileList *list = GNC_RECONCILE_LIST(clist);

  if (row == list->current_row)
    gnc_reconcile_list_toggle(list);

  GTK_CLIST_CLASS(parent_class)->unselect_row(clist, row, column, event);
}

static void
gnc_reconcile_list_destroy(GtkObject *object)
{
  GNCReconcileList *list = GNC_RECONCILE_LIST(object);

  if (list->reconciled_style != NULL)
  {
    gtk_style_unref(list->reconciled_style);
    list->reconciled_style = NULL;
  }

  if (list->reconciled != NULL)
  {
    g_free(list->reconciled);
    list->reconciled = NULL;
  }

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (* GTK_OBJECT_CLASS(parent_class)->destroy) (object);
}

gint
gnc_reconcile_list_get_row_height(GNCReconcileList *list)
{
  assert(GTK_IS_GNC_RECONCILE_LIST(list));

  if (!GTK_WIDGET_REALIZED(list))
    return 0;

  gtk_clist_set_row_height(GTK_CLIST(list), 0);

  return GTK_CLIST(list)->row_height;
}

gint
gnc_reconcile_list_get_num_splits(GNCReconcileList *list)
{
  assert(GTK_IS_GNC_RECONCILE_LIST(list));

  return list->num_splits;
}


/********************************************************************\
 * gnc_reconcile_list_refresh                                       *
 *   refreshes the list                                             *
 *                                                                  *
 * Args: list - list to refresh                                     *
 * Returns: nothing                                                 *
\********************************************************************/
void
gnc_reconcile_list_refresh(GNCReconcileList *list)
{
  GtkCList *clist = GTK_CLIST(list);

  assert(GTK_IS_GNC_RECONCILE_LIST(list));

  gtk_clist_freeze(clist);

  gtk_clist_clear(clist);
  list->num_splits = 0;
  if (list->reconciled != NULL)
    g_free(list->reconciled);

  /* This is too many, but it's simple */
  list->reconciled = g_new0(gboolean, xaccAccountGetNumSplits(list->account));

  gnc_reconcile_list_fill(list);

  gtk_clist_thaw(clist);

  gtk_clist_columns_autosize(clist);
}


/********************************************************************\
 * gnc_reconcile_list_reconciled_balance                            *
 *   returns the reconciled balance of the list                     *
 *                                                                  *
 * Args: list - list to get reconciled balance of                   *
 * Returns: reconciled balance (double)                             *
\********************************************************************/
double
gnc_reconcile_list_reconciled_balance(GNCReconcileList *list)
{
  GtkCList *clist = GTK_CLIST(list);
  Split *split;
  double total = 0.0;
  int account_type;
  char recn;
  int i;

  assert(GTK_IS_GNC_RECONCILE_LIST(list));

  if (list->reconciled == NULL)
    return 0.0;

  account_type = xaccAccountGetType(list->account);

  for (i = 0; i < list->num_splits; i++)
  {
    split = gtk_clist_get_row_data(clist, i);

    recn = xaccSplitGetReconcile(split);

    if (!list->reconciled[i])
      continue;

    if((account_type == STOCK) || (account_type == MUTUAL))
      total += xaccSplitGetShareAmount(split);
    else
      total += xaccSplitGetValue(split);
  }

  return DABS(total);
}


/********************************************************************\
 * gnc_reconcile_list_commit                                        *
 *   commit the reconcile information in the list                   *
 *                                                                  *
 * Args: list - list to commit                                      *
 * Returns: nothing                                                 *
\********************************************************************/
void
gnc_reconcile_list_commit(GNCReconcileList *list)
{
  GtkCList *clist = GTK_CLIST(list);
  Split *split;
  int i;

  assert(GTK_IS_GNC_RECONCILE_LIST(list));

  if (list->reconciled == NULL)
    return;

  for (i = 0; i < list->num_splits; i++)
    if (list->reconciled[i])
    {
      split = gtk_clist_get_row_data(clist, i);
      xaccSplitSetReconcile(split, YREC);
    }
}


static void
gnc_reconcile_list_fill(GNCReconcileList *list)
{
  gchar *strings[list->num_columns + 1];
  Transaction *trans;
  Split *split;
  int num_splits;
  int account_type;
  double amount;
  char recn_str[2];
  char recn;
  int row;
  int i;

  account_type = xaccAccountGetType(list->account);
  num_splits = xaccAccountGetNumSplits(list->account);
  strings[4] = recn_str;
  strings[5] = NULL;

  for (i = 0; i < num_splits; i++)
  {
    split = xaccAccountGetSplit(list->account, i);

    recn = xaccSplitGetReconcile(split);
    if ((recn != NREC) && (recn != CREC))
      continue;

    if((account_type == STOCK) || (account_type == MUTUAL))
      amount = xaccSplitGetShareAmount(split);
    else
      amount = xaccSplitGetValue(split);

    if ((amount < 0) && (list->list_type == RECLIST_CREDIT))
      continue;
    if ((amount >= 0) && (list->list_type == RECLIST_DEBIT))
      continue;

    trans = xaccSplitGetParent(split);

    strings[0] = xaccTransGetDateStr(trans);
    strings[1] = xaccTransGetNum(trans);
    strings[2] = xaccTransGetDescription(trans);
    strings[3] = g_strdup_printf("%.2f", DABS(amount));
    g_snprintf(recn_str, 2, "%c", list->reconciled[i] ? YREC : recn);

    row = gtk_clist_append(GTK_CLIST(list), strings);
    gtk_clist_set_row_data(GTK_CLIST(list), row, (gpointer) split);

    g_free(strings[3]);

    list->num_splits++;
  }
}
