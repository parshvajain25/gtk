/*
 * Copyright © 2018 Benjamin Otte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 */

#include "config.h"

#include "gtktim2sortmodel.h"

#include "gtkintl.h"
#include "gtkprivate.h"
#include "gtktimsortprivate.h"

typedef struct _SortItem SortItem;
struct _SortItem
{
  GObject *item;
  guint position;
};

static void
sort_item_clear (gpointer data)
{
  SortItem *si = data;

  g_clear_object (&si->item);
}

#define GDK_ARRAY_ELEMENT_TYPE SortItem
#define GDK_ARRAY_TYPE_NAME SortArray
#define GDK_ARRAY_NAME sort_array
#define GDK_ARRAY_FREE_FUNC sort_item_clear
#define GDK_ARRAY_BY_VALUE 1
#include "gdk/gdkarrayimpl.c"

/**
 * SECTION:gtksor4listmodel
 * @title: GtkTim2SortModel
 * @short_description: A list model that sorts its items
 * @see_also: #GListModel, #GtkSorter
 *
 * #GtkTim2SortModel is a list model that takes a list model and
 * sorts its elements according to a #GtkSorter.
 *
 * #GtkTim2SortModel is a generic model and because of that it
 * cannot take advantage of any external knowledge when sorting.
 * If you run into performance issues with #GtkTim2SortModel, it
 * is strongly recommended that you write your own sorting list
 * model.
 */

enum {
  PROP_0,
  PROP_INCREMENTAL,
  PROP_MODEL,
  PROP_SORTER,
  NUM_PROPERTIES
};

struct _GtkTim2SortModel
{
  GObject parent_instance;

  GListModel *model;
  GtkSorter *sorter;
  gboolean incremental;

  GtkTimSort sort; /* ongoing sort operation */
  guint sort_cb; /* 0 or current ongoing sort callback */
  SortArray items; /* empty if known unsorted */
};

struct _GtkTim2SortModelClass
{
  GObjectClass parent_class;
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

static GType
gtk_tim2_sort_model_get_item_type (GListModel *list)
{
  return G_TYPE_OBJECT;
}

static guint
gtk_tim2_sort_model_get_n_items (GListModel *list)
{
  GtkTim2SortModel *self = GTK_TIM2_SORT_MODEL (list);

  if (self->model == NULL)
    return 0;

  return g_list_model_get_n_items (self->model);
}

static gpointer
gtk_tim2_sort_model_get_item (GListModel *list,
                              guint       position)
{
  GtkTim2SortModel *self = GTK_TIM2_SORT_MODEL (list);

  if (self->model == NULL)
    return NULL;

  if (sort_array_is_empty (&self->items))
    return g_list_model_get_item (self->model, position);

  if (position >= sort_array_get_size (&self->items))
    return NULL;

  return g_object_ref (sort_array_get (&self->items, position)->item);
}

static void
gtk_tim2_sort_model_model_init (GListModelInterface *iface)
{
  iface->get_item_type = gtk_tim2_sort_model_get_item_type;
  iface->get_n_items = gtk_tim2_sort_model_get_n_items;
  iface->get_item = gtk_tim2_sort_model_get_item;
}

G_DEFINE_TYPE_WITH_CODE (GtkTim2SortModel, gtk_tim2_sort_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, gtk_tim2_sort_model_model_init))

static gboolean
gtk_tim2_sort_model_is_sorting (GtkTim2SortModel *self)
{
  return self->sort_cb != 0;
}

static void
gtk_tim2_sort_model_stop_sorting (GtkTim2SortModel *self,
                                  gsize            *runs)
{
  if (self->sort_cb == 0)
    {
      if (runs)
        {
          runs[0] = sort_array_get_size (&self->items);
          runs[1] = 0;
        }
      return;
    }

  if (runs)
    gtk_tim_sort_get_runs (&self->sort, runs);
  gtk_tim_sort_finish (&self->sort);
  g_clear_handle_id (&self->sort_cb, g_source_remove);
}

static gboolean
gtk_tim2_sort_model_sort_step (GtkTim2SortModel *self,
                               gboolean          finish,
                               guint            *out_position,
                               guint            *out_n_items)
{
  gint64 end_time = g_get_monotonic_time ();
  gboolean result = FALSE;
  GtkTimSortRun change;
  SortItem *start_change, *end_change;

  /* 1 millisecond */
  end_time += 1000;
  end_change = sort_array_get_data (&self->items);
  start_change = end_change + sort_array_get_size (&self->items);

  while (gtk_tim_sort_step (&self->sort, &change))
    {
      result = TRUE;
      if (change.len)
        {
          start_change = MIN (start_change, (SortItem *) change.base);
          end_change = MAX (end_change, ((SortItem *) change.base) + change.len);
        }
     
      if (g_get_monotonic_time () >= end_time && !finish)
        break;
    }

  if (start_change < end_change)
    {
      *out_position = start_change - sort_array_get_data (&self->items);
      *out_n_items = end_change - start_change;
    }
  else
    {
      *out_position = 0;
      *out_n_items = 0;
    }

  return result;
}

static gboolean
gtk_tim2_sort_model_sort_cb (gpointer data)
{
  GtkTim2SortModel *self = data;
  guint pos, n_items;

  if (gtk_tim2_sort_model_sort_step (self, FALSE, &pos, &n_items))
    {
      if (n_items)
        g_list_model_items_changed (G_LIST_MODEL (self), pos, n_items, n_items);
      return G_SOURCE_CONTINUE;
    }

  gtk_tim2_sort_model_stop_sorting (self, NULL);
  return G_SOURCE_REMOVE;
}

static int
sort_func (gconstpointer a,
           gconstpointer b,
           gpointer      data)
{
  SortItem *sa = (SortItem *) a;
  SortItem *sb = (SortItem *) b;

  return gtk_sorter_compare (data, sa->item, sb->item);
}

static gboolean
gtk_tim2_sort_model_start_sorting (GtkTim2SortModel *self,
                                   gsize            *runs)
{
  g_assert (self->sort_cb == 0);

  gtk_tim_sort_init (&self->sort,
                     sort_array_get_data (&self->items),
                     sort_array_get_size (&self->items),
                     sizeof (SortItem),
                     sort_func,
                     self->sorter);
  if (runs)
    gtk_tim_sort_set_runs (&self->sort, runs);
  if (self->incremental)
    gtk_tim_sort_set_max_merge_size (&self->sort, 1024);

  if (!self->incremental)
    return FALSE;

  self->sort_cb = g_idle_add (gtk_tim2_sort_model_sort_cb, self);
  return TRUE;
}

static void
gtk_tim2_sort_model_finish_sorting (GtkTim2SortModel *self,
                                    guint            *pos,
                                    guint            *n_items)
{
  gtk_tim_sort_set_max_merge_size (&self->sort, 0);

  gtk_tim2_sort_model_sort_step (self, TRUE, pos, n_items);
  gtk_tim_sort_finish (&self->sort);

  gtk_tim2_sort_model_stop_sorting (self, NULL);
}

static void
gtk_tim2_sort_model_clear_items (GtkTim2SortModel *self,
                                 guint            *pos,
                                 guint            *n_items)
{
  gtk_tim2_sort_model_stop_sorting (self, NULL);

  if (pos || n_items)
    {
      guint start, end;

      for (start = 0; start < sort_array_get_size (&self->items); start++)
        {
          if (sort_array_index (&self->items, start)->position != start)
            break;
        }
      for (end = sort_array_get_size (&self->items); end > start; end--)
        {
          if (sort_array_index (&self->items, end - 1)->position != end - 1)
            break;
        }

      *n_items = end - start;
      if (*n_items == 0)
        *pos = 0;
      else
        *pos = start;
    }

  sort_array_clear (&self->items);
} 

static gboolean
gtk_tim2_sort_model_should_sort (GtkTim2SortModel *self)
{
  return self->sorter != NULL &&
         self->model != NULL &&
         gtk_sorter_get_order (self->sorter) != GTK_SORTER_ORDER_NONE;
}

static void
gtk_tim2_sort_model_create_items (GtkTim2SortModel *self)
{
  guint i, n_items;

  if (!gtk_tim2_sort_model_should_sort (self))
    return;

  n_items = g_list_model_get_n_items (self->model);
  sort_array_reserve (&self->items, n_items);
  for (i = 0; i < n_items; i++)
    {
      sort_array_append (&self->items, &(SortItem) { g_list_model_get_item (self->model, i), i });
    }
}

static void
gtk_tim2_sort_model_update_items (GtkTim2SortModel *self,
                                  gsize             runs[GTK_TIM_SORT_MAX_PENDING + 1],
                                  guint             position,
                                  guint             removed,
                                  guint             added,
                                  guint            *unmodified_start,
                                  guint            *unmodified_end)
{
  guint i, n_items, valid;
  guint start, end;

  n_items = sort_array_get_size (&self->items);
  start = n_items;
  end = n_items;
  
  valid = 0;
  for (i = 0; i < n_items; i++)
    {
      SortItem *si = sort_array_index (&self->items, i);

      if (si->position >= position + removed)
        si->position = si->position - removed + added;
      else if (si->position >= position)
        { 
          start = MIN (start, valid);
          end = n_items - i - 1;
          sort_item_clear (si);
          continue;
        }
      if (valid < i)
        *sort_array_index (&self->items, valid) = *sort_array_index (&self->items, i);
      valid++;
    }

  /* FIXME */
  runs[0] = 0;

  g_assert (valid == n_items - removed);
  memset (sort_array_index (&self->items, valid), 0, sizeof (SortItem) * removed); 
  sort_array_set_size (&self->items, valid);

  *unmodified_start = start;
  *unmodified_end = end;
}

static void
gtk_tim2_sort_model_items_changed_cb (GListModel       *model,
                                      guint             position,
                                      guint             removed,
                                      guint             added,
                                      GtkTim2SortModel *self)
{
  gsize runs[GTK_TIM_SORT_MAX_PENDING + 1];
  guint i, n_items, start, end;
  gboolean was_sorting;

  if (removed == 0 && added == 0)
    return;

  if (!gtk_tim2_sort_model_should_sort (self))
    {
      g_list_model_items_changed (G_LIST_MODEL (self), position, removed, added);
      return;
    }

  was_sorting = gtk_tim2_sort_model_is_sorting (self);
  gtk_tim2_sort_model_stop_sorting (self, runs);

  gtk_tim2_sort_model_update_items (self, runs, position, removed, added, &start, &end);

  if (added > 0)
    {
      gboolean success;

      sort_array_reserve (&self->items, sort_array_get_size (&self->items) + added);
      for (i = position; i < position + added; i++)
        {
          sort_array_append (&self->items, &(SortItem) { g_list_model_get_item (self->model, i), i });
        }

      end = 0;
      success = gtk_tim2_sort_model_start_sorting (self, runs);
      if (!success)
        {
          guint pos, n;
          gtk_tim2_sort_model_finish_sorting (self, &pos, &n);
          start = MIN (start, pos);
          end = MIN (end, sort_array_get_size (&self->items) - pos - n);
        }
    }
  else
    {
      if (was_sorting)
        gtk_tim2_sort_model_start_sorting (self, runs);
    }

  n_items = sort_array_get_size (&self->items) - start - end;
  g_list_model_items_changed (G_LIST_MODEL (self), start, n_items - added + removed, n_items);
}

static void
gtk_tim2_sort_model_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GtkTim2SortModel *self = GTK_TIM2_SORT_MODEL (object);

  switch (prop_id)
    {
    case PROP_INCREMENTAL:
      gtk_tim2_sort_model_set_incremental (self, g_value_get_boolean (value));
      break;

    case PROP_MODEL:
      gtk_tim2_sort_model_set_model (self, g_value_get_object (value));
      break;

    case PROP_SORTER:
      gtk_tim2_sort_model_set_sorter (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void 
gtk_tim2_sort_model_get_property (GObject     *object,
                                  guint        prop_id,
                                  GValue      *value,
                                  GParamSpec  *pspec)
{
  GtkTim2SortModel *self = GTK_TIM2_SORT_MODEL (object);

  switch (prop_id)
    {
    case PROP_INCREMENTAL:
      g_value_set_boolean (value, self->incremental);
      break;

    case PROP_MODEL:
      g_value_set_object (value, self->model);
      break;

    case PROP_SORTER:
      g_value_set_object (value, self->sorter);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_tim2_sort_model_sorter_changed_cb (GtkSorter        *sorter,
                                       int               change,
                                       GtkTim2SortModel *self)
{
  guint pos, n_items;

  if (gtk_sorter_get_order (sorter) == GTK_SORTER_ORDER_NONE)
    gtk_tim2_sort_model_clear_items (self, &pos, &n_items);
  else
    {
      if (sort_array_is_empty (&self->items))
        gtk_tim2_sort_model_create_items (self);

      gtk_tim2_sort_model_stop_sorting (self, NULL);

      if (gtk_tim2_sort_model_start_sorting (self, NULL))
        pos = n_items = 0;
      else
        gtk_tim2_sort_model_finish_sorting (self, &pos, &n_items);
    }

  if (n_items > 0)
    g_list_model_items_changed (G_LIST_MODEL (self), pos, n_items, n_items);
}

static void
gtk_tim2_sort_model_clear_model (GtkTim2SortModel *self)
{
  if (self->model == NULL)
    return;

  g_signal_handlers_disconnect_by_func (self->model, gtk_tim2_sort_model_items_changed_cb, self);
  g_clear_object (&self->model);
  gtk_tim2_sort_model_clear_items (self, NULL, NULL);
}

static void
gtk_tim2_sort_model_clear_sorter (GtkTim2SortModel *self)
{
  if (self->sorter == NULL)
    return;

  g_signal_handlers_disconnect_by_func (self->sorter, gtk_tim2_sort_model_sorter_changed_cb, self);
  g_clear_object (&self->sorter);
}

static void
gtk_tim2_sort_model_dispose (GObject *object)
{
  GtkTim2SortModel *self = GTK_TIM2_SORT_MODEL (object);

  gtk_tim2_sort_model_clear_model (self);
  gtk_tim2_sort_model_clear_sorter (self);

  G_OBJECT_CLASS (gtk_tim2_sort_model_parent_class)->dispose (object);
};

static void
gtk_tim2_sort_model_class_init (GtkTim2SortModelClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);

  gobject_class->set_property = gtk_tim2_sort_model_set_property;
  gobject_class->get_property = gtk_tim2_sort_model_get_property;
  gobject_class->dispose = gtk_tim2_sort_model_dispose;

  /**
   * GtkTim2SortModel:incremental:
   *
   * If the model should sort items incrementally
   */
  properties[PROP_INCREMENTAL] =
      g_param_spec_boolean ("incremental",
                            P_("Incremental"),
                            P_("Sort items incrementally"),
                            FALSE,
                            GTK_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GtkTim2SortModel:model:
   *
   * The model being sorted
   */
  properties[PROP_MODEL] =
      g_param_spec_object ("model",
                           P_("Model"),
                           P_("The model being sorted"),
                           G_TYPE_LIST_MODEL,
                           GTK_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GtkTim2SortModel:sorter:
   *
   * The sorter for this model
   */
  properties[PROP_SORTER] =
      g_param_spec_object ("sorter",
                            P_("Sorter"),
                            P_("The sorter for this model"),
                            GTK_TYPE_SORTER,
                            GTK_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (gobject_class, NUM_PROPERTIES, properties);
}

static void
gtk_tim2_sort_model_init (GtkTim2SortModel *self)
{
}

/**
 * gtk_tim2_sort_model_new:
 * @model: (allow-none): the model to sort
 * @sorter: (allow-none): the #GtkSorter to sort @model with
 *
 * Creates a new sort list model that uses the @sorter to sort @model.
 *
 * Returns: a new #GtkTim2SortModel
 **/
GtkTim2SortModel *
gtk_tim2_sort_model_new (GListModel *model,
                         GtkSorter  *sorter)
{
  GtkTim2SortModel *result;

  g_return_val_if_fail (model == NULL || G_IS_LIST_MODEL (model), NULL);
  g_return_val_if_fail (sorter == NULL || GTK_IS_SORTER (sorter), NULL);

  result = g_object_new (GTK_TYPE_TIM2_SORT_MODEL,
                         "model", model,
                         "sorter", sorter,
                         NULL);

  return result;
}

/**
 * gtk_tim2_sort_model_set_model:
 * @self: a #GtkTim2SortModel
 * @model: (allow-none): The model to be sorted
 *
 * Sets the model to be sorted. The @model's item type must conform to
 * the item type of @self.
 **/
void
gtk_tim2_sort_model_set_model (GtkTim2SortModel *self,
                               GListModel       *model)
{
  guint removed, added;

  g_return_if_fail (GTK_IS_TIM2_SORT_MODEL (self));
  g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

  if (self->model == model)
    return;

  removed = g_list_model_get_n_items (G_LIST_MODEL (self));
  gtk_tim2_sort_model_clear_model (self);

  if (model)
    {
      guint ignore1, ignore2;

      self->model = g_object_ref (model);
      g_signal_connect (model, "items-changed", G_CALLBACK (gtk_tim2_sort_model_items_changed_cb), self);
      added = g_list_model_get_n_items (model);

      gtk_tim2_sort_model_create_items (self);
      if (!gtk_tim2_sort_model_start_sorting (self, NULL))
        gtk_tim2_sort_model_finish_sorting (self, &ignore1, &ignore2);
    }
  else
    added = 0;
  
  if (removed > 0 || added > 0)
    g_list_model_items_changed (G_LIST_MODEL (self), 0, removed, added);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}

/**
 * gtk_tim2_sort_model_get_model:
 * @self: a #GtkTim2SortModel
 *
 * Gets the model currently sorted or %NULL if none.
 *
 * Returns: (nullable) (transfer none): The model that gets sorted
 **/
GListModel *
gtk_tim2_sort_model_get_model (GtkTim2SortModel *self)
{
  g_return_val_if_fail (GTK_IS_TIM2_SORT_MODEL (self), NULL);

  return self->model;
}

/**
 * gtk_tim2_sort_model_set_sorter:
 * @self: a #GtkTim2SortModel
 * @sorter: (allow-none): the #GtkSorter to sort @model with
 *
 * Sets a new sorter on @self.
 */
void
gtk_tim2_sort_model_set_sorter (GtkTim2SortModel *self,
                                GtkSorter        *sorter)
{
  g_return_if_fail (GTK_IS_TIM2_SORT_MODEL (self));
  g_return_if_fail (sorter == NULL || GTK_IS_SORTER (sorter));

  gtk_tim2_sort_model_clear_sorter (self);

  if (sorter)
    {
      self->sorter = g_object_ref (sorter);
      g_signal_connect (sorter, "changed", G_CALLBACK (gtk_tim2_sort_model_sorter_changed_cb), self);
      gtk_tim2_sort_model_sorter_changed_cb (sorter, GTK_SORTER_CHANGE_DIFFERENT, self);
    }
  else
    {
      guint n_items = g_list_model_get_n_items (G_LIST_MODEL (self));
      if (n_items > 1)
        g_list_model_items_changed (G_LIST_MODEL (self), 0, n_items, n_items);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SORTER]);
}

/**
 * gtk_tim2_sort_model_get_sorter:
 * @self: a #GtkTim2SortModel
 *
 * Gets the sorter that is used to sort @self.
 *
 * Returns: (nullable) (transfer none): the sorter of #self
 */
GtkSorter *
gtk_tim2_sort_model_get_sorter (GtkTim2SortModel *self)
{
  g_return_val_if_fail (GTK_IS_TIM2_SORT_MODEL (self), NULL);

  return self->sorter;
}

/**
 * gtk_tim2_sort_model_set_incremental:
 * @self: a #GtkTim2SortModel
 * @incremental: %TRUE to sort incrementally
 *
 * Sets the sort model to do an incremental sort.
 *
 * When incremental sorting is enabled, the sortlistmodel will not do
 * a complete sort immediately, but will instead queue an idle handler that
 * incrementally sorts the items towards their correct position. This of
 * course means that items do not instantly appear in the right place. It
 * also means that the total sorting time is a lot slower.
 *
 * When your filter blocks the UI while sorting, you might consider
 * turning this on. Depending on your model and sorters, this may become
 * interesting around 10,000 to 100,000 items.
 *
 * By default, incremental sortinging is disabled.
 */
void
gtk_tim2_sort_model_set_incremental (GtkTim2SortModel *self,
                                     gboolean          incremental)
{
  g_return_if_fail (GTK_IS_TIM2_SORT_MODEL (self));

  if (self->incremental == incremental)
    return;

  self->incremental = incremental;

  if (!incremental && gtk_tim2_sort_model_is_sorting (self))
    {
      guint pos, n_items;

      gtk_tim2_sort_model_finish_sorting (self, &pos, &n_items);
      if (n_items)
        g_list_model_items_changed (G_LIST_MODEL (self), pos, n_items, n_items);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INCREMENTAL]);
}

/**
 * gtk_tim2_sort_model_get_incremental:
 * @self: a #GtkModel
 *
 * Returns whether incremental sorting was enabled via
 * gtk_sort_list_model_set_incremental().
 *
 * Returns: %TRUE if incremental sorting is enabled
 */
gboolean
gtk_tim2_sort_model_get_incremental (GtkTim2SortModel *self)
{
  g_return_val_if_fail (GTK_IS_TIM2_SORT_MODEL (self), FALSE);

  return self->incremental;
}