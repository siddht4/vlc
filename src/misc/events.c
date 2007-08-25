/*****************************************************************************
 * events.c: events interface
 * This library provides an interface to the send and receive events.
 * It is more lightweight than variable based callback.
 * Methode
 *****************************************************************************
 * Copyright (C) 1998-2005 the VideoLAN team
 * $Id$
 *
 * Authors: Pierre d'Herbemont <pdherbemont # videolan.org >
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#include <vlc/vlc.h>

#include <assert.h>

#include <vlc_events.h>
#include <vlc_arrays.h>

/*****************************************************************************
 * Documentation : Read vlc_events.h
 *****************************************************************************/

//#define DEBUG_EVENT

/*****************************************************************************
 *  Private types.
 *****************************************************************************/

typedef struct vlc_event_listener_t
{
    void *               p_user_data;
    vlc_event_callback_t pf_callback;
#ifdef DEBUG_EVENT
    char *               psz_debug_name;
#endif
} vlc_event_listener_t;

typedef struct vlc_event_listeners_group_t
{
    vlc_event_type_t    event_type;
    DECL_ARRAY(struct vlc_event_listener_t *) listeners;
} vlc_event_listeners_group_t;

#ifdef DEBUG_EVENT
static const char * ppsz_event_type_to_name[] = 
{
    [vlc_InputItemMetaChanged]          = "vlc_InputItemMetaChanged",
    [vlc_InputItemSubItemAdded]         = "vlc_InputItemSubItemAdded",
    [vlc_ServicesDiscoveryItemAdded]    = "vlc_ServicesDiscoveryItemAdded",
    [vlc_ServicesDiscoveryItemRemoved]  = "vlc_ServicesDiscoveryItemRemoved"
};
#endif

/*****************************************************************************
 * 
 *****************************************************************************/

/**
 * Initialize event manager object
 * p_obj is the object that contains the event manager. But not
 * necessarily a vlc_object_t (an input_item_t is not a vlc_object_t
 * for instance).
 * p_parent_obj gives a libvlc instance
 */
int vlc_event_manager_init( vlc_event_manager_t * p_em, void * p_obj,
                            vlc_object_t * p_parent_obj )
{
    p_em->p_obj = p_obj;
    p_em->p_parent_object = p_parent_obj;
    vlc_mutex_init( p_parent_obj, &p_em->object_lock );
    ARRAY_INIT( p_em->listeners_groups );
    return VLC_SUCCESS;
}

/**
 * Destroy the event manager
 */
void vlc_event_manager_fini( vlc_event_manager_t * p_em )
{
    struct vlc_event_listeners_group_t * listeners_group;
    struct vlc_event_listener_t * listener;

    vlc_mutex_destroy( &p_em->object_lock );

    FOREACH_ARRAY( listeners_group, p_em->listeners_groups )
        FOREACH_ARRAY( listener, listeners_group->listeners )
            free( listener );
        FOREACH_END()
        free( listeners_group );
    FOREACH_END()
}

/**
 * Destroy the event manager
 */
int vlc_event_manager_register_event_type(
        vlc_event_manager_t * p_em,
        vlc_event_type_t event_type )
{
    vlc_event_listeners_group_t * listeners_group;
    listeners_group = malloc(sizeof(vlc_event_listeners_group_t));

    if( !listeners_group )
        return VLC_ENOMEM;

    listeners_group->event_type = event_type;
    ARRAY_INIT( listeners_group->listeners );
    
    vlc_mutex_lock( &p_em->object_lock );
    ARRAY_APPEND( p_em->listeners_groups, listeners_group );
    vlc_mutex_unlock( &p_em->object_lock );

    return VLC_SUCCESS;
}

/**
 * Send an event to the listener attached to this p_em.
 */
void vlc_event_send( vlc_event_manager_t * p_em,
                     vlc_event_t * p_event )
{
    vlc_event_listeners_group_t * listeners_group;
    vlc_event_listener_t * listener;
    vlc_event_callback_t func = NULL;
    void * user_data = NULL;

    /* Fill event with the sending object now */
    p_event->p_obj = p_em->p_obj;

    vlc_mutex_lock( &p_em->object_lock );
    FOREACH_ARRAY( listeners_group, p_em->listeners_groups )
        if( listeners_group->event_type == p_event->type )
        {
            /* We found the group, now send every one the event */
            FOREACH_ARRAY( listener, listeners_group->listeners )
                func = listener->pf_callback;
                user_data = listener->p_user_data;
#ifdef DEBUG_EVENT
                msg_Dbg( p_em->p_parent_object,
                    "Calling '%s' with a '%s' event (data %p)",
                    listener->psz_debug_name,
                    ppsz_event_type_to_name[p_event->type],
                    listener->p_user_data );
#endif
                /* This is safe to do that because we are sure 
                 * that there will be no object owned references
                 * used after the lock. */
                vlc_mutex_unlock( &p_em->object_lock );
                func( p_event, user_data );
                vlc_mutex_lock( &p_em->object_lock );
            FOREACH_END()
            break;
        }
    FOREACH_END()
    vlc_mutex_unlock( &p_em->object_lock );
}

/**
 * Add a callback for an event.
 */
int __vlc_event_attach( vlc_event_manager_t * p_em,
                        vlc_event_type_t event_type,
                        vlc_event_callback_t pf_callback,
                        void *p_user_data,
                        const char * psz_debug_name )
{
    vlc_event_listeners_group_t * listeners_group;
    vlc_event_listener_t * listener;
    listener = malloc(sizeof(vlc_event_listener_t));
    if( !listener )
        return VLC_ENOMEM;
    
    listener->p_user_data = p_user_data;
    listener->pf_callback = pf_callback;
#ifdef DEBUG_EVENT
    listener->psz_debug_name = strdup( psz_debug_name );
#else
    (void)psz_debug_name;
#endif

    vlc_mutex_lock( &p_em->object_lock );
    FOREACH_ARRAY( listeners_group, p_em->listeners_groups )
        if( listeners_group->event_type == event_type )
        {
            ARRAY_APPEND( listeners_group->listeners, listener );
#ifdef DEBUG_EVENT
                msg_Dbg( p_em->p_parent_object,
                    "Listening to '%s' event with '%s' (data %p)",
                    ppsz_event_type_to_name[event_type],
                    listener->psz_debug_name,
                    listener->p_user_data );
#endif
            vlc_mutex_unlock( &p_em->object_lock );
            return VLC_SUCCESS;
        }
    FOREACH_END()
    vlc_mutex_unlock( &p_em->object_lock );

    msg_Err( p_em->p_parent_object, "Can't attach to an object event manager event" );
    free(listener);
    return VLC_EGENERIC;
}

/**
 * Remove a callback for an event.
 */
int vlc_event_detach( vlc_event_manager_t *p_em,
                      vlc_event_type_t event_type,
                      vlc_event_callback_t pf_callback,
                      void *p_user_data )
{
    vlc_event_listeners_group_t * listeners_group;
    struct vlc_event_listener_t * listener;

    vlc_mutex_lock( &p_em->object_lock );
    FOREACH_ARRAY( listeners_group, p_em->listeners_groups )
        if( listeners_group->event_type == event_type )
        {
            FOREACH_ARRAY( listener, listeners_group->listeners )
                if( listener->pf_callback == pf_callback &&
                    listener->p_user_data == p_user_data )
                {
                    /* that's our listener */
                    ARRAY_REMOVE( listeners_group->listeners,
                        fe_idx /* This comes from the macro (and that's why
                                  I hate macro) */ );
#ifdef DEBUG_EVENT
                    msg_Dbg( p_em->p_parent_object,
                        "Detaching '%s' from '%s' event (data %p)",
                        listener->psz_debug_name,
                        ppsz_event_type_to_name[event_type],
                        listener->p_user_data );

                    free( listener->psz_debug_name );
#endif
                    free( listener );
                    vlc_mutex_unlock( &p_em->object_lock );
                    return VLC_SUCCESS;
                }
            FOREACH_END()
        }
    FOREACH_END()
    vlc_mutex_unlock( &p_em->object_lock );

    msg_Warn( p_em->p_parent_object, "Can't detach to an object event manager event" );

    return VLC_EGENERIC;
}
