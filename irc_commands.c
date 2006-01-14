  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2006 Wilmer van der Gaast and others                *
  \********************************************************************/

/* IRC commands                                                         */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License with
  the Debian GNU/Linux distribution in /usr/share/common-licenses/GPL;
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#define BITLBEE_CORE
#include "bitlbee.h"

static int irc_cmd_pass( irc_t *irc, char **cmd )
{
	if( global.conf->auth_pass && strcmp( cmd[1], global.conf->auth_pass ) == 0 )
	{
		irc->status = USTATUS_AUTHORIZED;
		irc_check_login();
	}
	else
	{
		irc_reply( irc, 464, ":Incorrect password" );
	}
	
	return( 1 );
}

static int irc_cmd_user( irc_t *irc, char **cmd )
{
	irc->user = g_strdup( cmd[1] );
	irc->realname = g_strdup( cmd[4] );
	
	irc_check_login( irc );
	
	return( 1 );
}

static int irc_cmd_nick( irc_t *irc, char **cmd )
{
	if( irc->nick )
	{
		irc_reply( irc, 438, ":The hand of the deity is upon thee, thy nick may not change" );
	}
	/* This is not clean, but for now it'll have to be like this... */
	else if( ( nick_cmp( cmd[1], irc->mynick ) == 0 ) || ( nick_cmp( cmd[1], NS_NICK ) == 0 ) )
	{
		irc_reply( irc, 433, ":This nick is already in use" );
	}
	else if( !nick_ok( cmd[1] ) )
	{
		/* [SH] Invalid characters. */
		irc_reply( irc, 432, ":This nick contains invalid characters" );
	}
	else
	{
		irc->nick = g_strdup( cmd[1] );
		
		irc_check_login( irc );
	}
	
	return( 1 );
}

static int irc_cmd_quit( irc_t *irc, char **cmd )
{
	irc_write( irc, "ERROR :%s%s", cmd[1]?"Quit: ":"", cmd[1]?cmd[1]:"Client Quit" );
	g_io_channel_close( irc->io_channel );
	
	return( 0 );
}

static int irc_cmd_ping( irc_t *irc, char **cmd )
{
	irc_write( irc, ":%s PONG %s :%s", irc->myhost, irc->myhost, cmd[1]?cmd[1]:irc->myhost );
	
	return( 1 );
}

static int irc_cmd_oper( irc_t *irc, char **cmd )
{
	if( global.conf->oper_pass && strcmp( cmd[2], global.conf->oper_pass ) == 0 )
	{
		irc_umode_set( irc, "+o", 1 );
		irc_reply( irc, 381, ":Password accepted" );
	}
	else
	{
		irc_reply( irc, 432, ":Incorrect password" );
	}
	
	return( 1 );
}

static int irc_cmd_mode( irc_t *irc, char **cmd )
{
	if( *cmd[1] == '#' || *cmd[1] == '&' )
	{
		if( cmd[2] )
		{
			if( *cmd[2] == '+' || *cmd[2] == '-' )
				irc_reply( irc, 477, "%s :Can't change channel modes", cmd[1] );
			else if( *cmd[2] == 'b' )
				irc_reply( irc, 368, "%s :No bans possible", cmd[1] );
		}
		else
			irc_reply( irc, 324, "%s +%s", cmd[1], CMODE );
	}
	else
	{
		if( nick_cmp( cmd[1], irc->nick ) == 0 )
		{
			if( cmd[2] )
				irc_umode_set( irc, cmd[2], 0 );
		}
		else
			irc_reply( irc, 502, ":Don't touch their modes" );
	}
	
	return( 1 );
}

static int irc_cmd_names( irc_t *irc, char **cmd )
{
	irc_names( irc, cmd[1]?cmd[1]:irc->channel );
	
	return( 1 );
}

static int irc_cmd_part( irc_t *irc, char **cmd )
{
	struct conversation *c;
	
	if( g_strcasecmp( cmd[1], irc->channel ) == 0 )
	{
		user_t *u = user_find( irc, irc->nick );
		
		/* Not allowed to leave control channel */
		irc_part( irc, u, irc->channel );
		irc_join( irc, u, irc->channel );
	}
	else if( ( c = conv_findchannel( cmd[1] ) ) )
	{
		user_t *u = user_find( irc, irc->nick );
		
		irc_part( irc, u, c->channel );
		
		if( c->gc && c->gc->prpl )
		{
			c->joined = 0;
			c->gc->prpl->chat_leave( c->gc, c->id );
		}
	}
	else
	{
		irc_reply( irc, 403, "%s :No such channel", cmd[1] );
	}
	
	return( 1 );
}

static int irc_cmd_join( irc_t *irc, char **cmd )
{
	if( g_strcasecmp( cmd[1], irc->channel ) == 0 )
		; /* Dude, you're already there...
		     RFC doesn't have any reply for that though? */
	else if( cmd[1] )
	{
		if( ( cmd[1][0] == '#' || cmd[1][0] == '&' ) && cmd[1][1] )
		{
			user_t *u = user_find( irc, cmd[1] + 1 );
			
			if( u && u->gc && u->gc->prpl && u->gc->prpl->chat_open )
			{
				irc_reply( irc, 403, "%s :Initializing groupchat in a different channel", cmd[1] );
				
				if( !u->gc->prpl->chat_open( u->gc, u->handle ) )
				{
					irc_usermsg( irc, "Could not open a groupchat with %s, maybe you don't have a connection to him/her yet?", u->nick );
				}
			}
			else if( u )
			{
				irc_reply( irc, 403, "%s :Groupchats are not possible with %s", cmd[1], cmd[1]+1 );
			}
			else
			{
				irc_reply( irc, 403, "%s :No such nick", cmd[1] );
			}
		}
		else
		{
			irc_reply( irc, 403, "%s :No such channel", cmd[1] );
		}
	}
	
	return( 1 );
}

static int irc_cmd_invite( irc_t *irc, char **cmd )
{
	char *nick = cmd[1], *channel = cmd[2];
	struct conversation *c = conv_findchannel( channel );
	user_t *u = user_find( irc, nick );
	
	if( u && c && ( u->gc == c->gc ) )
		if( c->gc && c->gc->prpl && c->gc->prpl->chat_invite )
		{
			c->gc->prpl->chat_invite( c->gc, c->id, "", u->handle );
			irc_reply( irc, 341, "%s %s", nick, channel );
			return( 1 );
		}
	
	irc_reply( irc, 482, "%s :Invite impossible; User/Channel non-existent or incompatible", channel );
	
	return( 1 );
}

/* TODO: Attach this one to NOTICE too! */
static int irc_cmd_privmsg( irc_t *irc, char **cmd )
{
	if ( !cmd[2] ) 
	{
		irc_reply( irc, 412, ":No text to send" );
	}
	else if ( irc->nick && g_strcasecmp( cmd[1], irc->nick ) == 0 ) 
	{
		irc_write( irc, ":%s!%s@%s %s %s :%s", irc->nick, irc->user, irc->host, cmd[0], cmd[1], cmd[2] ); 
	}
	else 
	{
		if( g_strcasecmp( cmd[1], irc->channel ) == 0 )
		{
			unsigned int i;
			char *t = set_getstr( irc, "default_target" );
			
			if( g_strcasecmp( t, "last" ) == 0 && irc->last_target )
				cmd[1] = irc->last_target;
			else if( g_strcasecmp( t, "root" ) == 0 )
				cmd[1] = irc->mynick;
			
			for( i = 0; i < strlen( cmd[2] ); i ++ )
			{
				if( cmd[2][i] == ' ' ) break;
				if( cmd[2][i] == ':' || cmd[2][i] == ',' )
				{
					cmd[1] = cmd[2];
					cmd[2] += i;
					*cmd[2] = 0;
					while( *(++cmd[2]) == ' ' );
					break;
				}
			}
			
			irc->is_private = 0;
			
			if( cmd[1] != irc->last_target )
			{
				if( irc->last_target )
					g_free( irc->last_target );
				irc->last_target = g_strdup( cmd[1] );
			}
		}
		else
		{
			irc->is_private = 1;
		}
		irc_send( irc, cmd[1], cmd[2], ( g_strcasecmp( cmd[0], "NOTICE" ) == 0 ) ? IM_FLAG_AWAY : 0 );
	}
	
	return( 1 );
}

static int irc_cmd_who( irc_t *irc, char **cmd )
{
	char *channel = cmd[1];
	user_t *u = irc->users;
	struct conversation *c;
	GList *l;
	
	if( !channel || *channel == '0' || *channel == '*' || !*channel )
		while( u )
		{
			irc_reply( irc, 352, "%s %s %s %s %s %c :0 %s", u->online ? irc->channel : "*", u->user, u->host, irc->myhost, u->nick, u->online ? ( u->away ? 'G' : 'H' ) : 'G', u->realname );
			u = u->next;
		}
	else if( g_strcasecmp( channel, irc->channel ) == 0 )
		while( u )
		{
			if( u->online )
				irc_reply( irc, 352, "%s %s %s %s %s %c :0 %s", channel, u->user, u->host, irc->myhost, u->nick, u->away ? 'G' : 'H', u->realname );
			u = u->next;
		}
	else if( ( c = conv_findchannel( channel ) ) )
		for( l = c->in_room; l; l = l->next )
		{
			if( ( u = user_findhandle( c->gc, l->data ) ) )
				irc_reply( irc, 352, "%s %s %s %s %s %c :0 %s", channel, u->user, u->host, irc->myhost, u->nick, u->away ? 'G' : 'H', u->realname );
		}
	else if( ( u = user_find( irc, channel ) ) )
		irc_reply( irc, 352, "%s %s %s %s %s %c :0 %s", channel, u->user, u->host, irc->myhost, u->nick, u->online ? ( u->away ? 'G' : 'H' ) : 'G', u->realname );
	
	irc_reply( irc, 315, "%s :End of /WHO list", channel?channel:"**" );
	
	return( 1 );
}

static int irc_cmd_userhost( irc_t *irc, char **cmd )
{
	user_t *u;
	int i;
	
	/* [TV] Usable USERHOST-implementation according to
		RFC1459. Without this, mIRC shows an error
		while connecting, and the used way of rejecting
		breaks standards.
	*/
	
	for( i = 1; cmd[i]; i ++ )
		if( ( u = user_find( irc, cmd[i] ) ) )
		{
			if( u->online && u->away )
				irc_reply( irc, 302, ":%s=-%s@%s", u->nick, u->user, u->host );
			else
				irc_reply( irc, 302, ":%s=+%s@%s", u->nick, u->user, u->host );
		}
	
	return( 1 );
}

static int irc_cmd_ison( irc_t *irc, char **cmd )
{
	user_t *u;
	char buff[IRC_MAX_LINE];
	int lenleft, i;
	
	buff[0] = '\0';
	
	/* [SH] Leave room for : and \0 */
	lenleft = IRC_MAX_LINE - 2;
	
	for( i = 1; cmd[i]; i ++ )
	{
		if( ( u = user_find( irc, cmd[i] ) ) && u->online )
		{
			/* [SH] Make sure we don't use too much buffer space. */
			lenleft -= strlen( u->nick ) + 1;
			
			if( lenleft < 0 )
			{
				break;
			}
			
			/* [SH] Add the nick to the buffer. Note
			 * that an extra space is always added. Even
			 * if it's the last nick in the list. Who
			 * cares?
			 */
			
			strcat( buff, u->nick );
			strcat( buff, " " );
		}
	}
	
	/* [WvG] Well, maybe someone cares, so why not remove it? */
	if( strlen( buff ) > 0 )
		buff[strlen(buff)-1] = '\0';
	
	irc_reply( irc, 303, ":%s", buff );
	
	return( 1 );
}

static int irc_cmd_watch( irc_t *irc, char **cmd )
{
	int i;
	
	/* Obviously we could also mark a user structure as being
	   watched, but what if the WATCH command is sent right
	   after connecting? The user won't exist yet then... */
	for( i = 1; cmd[i]; i ++ )
	{
		char *nick;
		user_t *u;
		
		if( !cmd[i][0] || !cmd[i][1] )
			break;
		
		nick = g_strdup( cmd[i] + 1 );
		nick_lc( nick );
		
		u = user_find( irc, nick );
		
		if( cmd[i][0] == '+' )
		{
			if( !g_hash_table_lookup( irc->watches, nick ) )
				g_hash_table_insert( irc->watches, nick, nick );
			
			if( u && u->online )
				irc_reply( irc, 604, "%s %s %s %d :%s", u->nick, u->user, u->host, time( NULL ), "is online" );
			else
				irc_reply( irc, 605, "%s %s %s %d :%s", nick, "*", "*", time( NULL ), "is offline" );
		}
		else if( cmd[i][0] == '-' )
		{
			gpointer okey, ovalue;
			
			if( g_hash_table_lookup_extended( irc->watches, nick, &okey, &ovalue ) )
			{
				g_free( okey );
				g_hash_table_remove( irc->watches, okey );
				
				irc_reply( irc, 602, "%s %s %s %d :%s", nick, "*", "*", 0, "Stopped watching" );
			}
		}
	}
	
	return( 1 );
}

static int irc_cmd_topic( irc_t *irc, char **cmd )
{
	if( cmd[2] )
		irc_reply( irc, 482, "%s :Cannot change topic", cmd[1] );
	else
		irc_topic( irc, cmd[1] );
	
	return( 1 );
}

static int irc_cmd_away( irc_t *irc, char **cmd )
{
	user_t *u = user_find( irc, irc->nick );
	GSList *c = get_connections();
	char *away = cmd[1];
	
	if( !u ) return( 1 );
	
	if( away && *away )
	{
		int i, j;
		
		/* Copy away string, but skip control chars. Mainly because
		   Jabber really doesn't like them. */
		u->away = g_malloc( strlen( away ) + 1 );
		for( i = j = 0; away[i]; i ++ )
			if( ( u->away[j] = away[i] ) >= ' ' )
				j ++;
		u->away[j] = 0;
		
		irc_reply( irc, 306, ":You're now away: %s", u->away );
		/* irc_umode_set( irc, irc->myhost, "+a" ); */
	}
	else
	{
		if( u->away ) g_free( u->away );
		u->away = NULL;
		/* irc_umode_set( irc, irc->myhost, "-a" ); */
		irc_reply( irc, 305, ":Welcome back" );
	}
	
	while( c )
	{
		if( ((struct gaim_connection *)c->data)->flags & OPT_LOGGED_IN )
			proto_away( c->data, u->away );
		
		c = c->next;
	}
	
	return( 1 );
}

static int irc_cmd_whois( irc_t *irc, char **cmd )
{
	char *nick = cmd[1];
	user_t *u = user_find( irc, nick );
	
	if( u )
	{
		irc_reply( irc, 311, "%s %s %s * :%s", u->nick, u->user, u->host, u->realname );
		
		if( u->gc )
			irc_reply( irc, 312, "%s %s.%s :%s network", u->nick, u->gc->user->username,
			           *u->gc->user->proto_opt[0] ? u->gc->user->proto_opt[0] : "", u->gc->prpl->name );
		else
			irc_reply( irc, 312, "%s %s :%s", u->nick, irc->myhost, IRCD_INFO );
		
		if( !u->online )
			irc_reply( irc, 301, "%s :%s", u->nick, "User is offline" );
		else if( u->away )
			irc_reply( irc, 301, "%s :%s", u->nick, u->away );
		
		irc_reply( irc, 318, "%s :End of /WHOIS list", nick );
	}
	else
	{
		irc_reply( irc, 401, "%s :Nick does not exist", nick );
	}
	
	return( 1 );
}

static int irc_cmd_whowas( irc_t *irc, char **cmd )
{
	/* For some reason irssi tries a whowas when whois fails. We can
	   ignore this, but then the user never gets a "user not found"
	   message from irssi which is a bit annoying. So just respond
	   with not-found and irssi users will get better error messages */
	
	irc_reply( irc, 406, "%s :Nick does not exist", cmd[1] );
	irc_reply( irc, 369, "%s :End of WHOWAS", cmd[1] );
	
	return( 1 );
}

/* TODO: Alias to NS */
static int irc_cmd_nickserv( irc_t *irc, char **cmd )
{
	/* [SH] This aliases the NickServ command to PRIVMSG root */
	/* [TV] This aliases the NS command to PRIVMSG root as well */
	root_command( irc, cmd + 1 );
	
	return( 1 );
}

static int irc_cmd_motd( irc_t *irc, char **cmd )
{
	irc_motd( irc );
	
	return( 1 );
}

static int irc_cmd_pong( irc_t *irc, char **cmd )
{
	/* We could check the value we get back from the user, but in
	   fact we don't care, we're just happy he's still alive. */
	irc->last_pong = gettime();
	irc->pinging = 0;
	
	return( 1 );
}

static int irc_cmd_completions( irc_t *irc, char **cmd )
{
	user_t *u = user_find( irc, irc->mynick );
	help_t *h;
	set_t *s;
	int i;
	
	irc_privmsg( irc, u, "NOTICE", irc->nick, "COMPLETIONS ", "OK" );
	
	for( i = 0; commands[i].command; i ++ )
		irc_privmsg( irc, u, "NOTICE", irc->nick, "COMPLETIONS ", commands[i].command );
	
	for( h = global.help; h; h = h->next )
		irc_privmsg( irc, u, "NOTICE", irc->nick, "COMPLETIONS help ", h->string );
	
	for( s = irc->set; s; s = s->next )
		irc_privmsg( irc, u, "NOTICE", irc->nick, "COMPLETIONS set ", s->key );
	
	irc_privmsg( irc, u, "NOTICE", irc->nick, "COMPLETIONS ", "END" );
	
	return( 1 );
}

static const command_t irc_commands[] = {
	{ "pass",        1, irc_cmd_pass,        IRC_CMD_PRE_LOGIN },
	{ "user",        4, irc_cmd_user,        IRC_CMD_PRE_LOGIN },
	{ "nick",        1, irc_cmd_nick,        0 },
	{ "quit",        0, irc_cmd_quit,        0 },
	{ "ping",        0, irc_cmd_ping,        0 },
	{ "oper",        2, irc_cmd_oper,        IRC_CMD_LOGGED_IN },
	{ "mode",        1, irc_cmd_mode,        IRC_CMD_LOGGED_IN },
	{ "names",       0, irc_cmd_names,       IRC_CMD_LOGGED_IN },
	{ "part",        1, irc_cmd_part,        IRC_CMD_LOGGED_IN },
	{ "join",        1, irc_cmd_join,        IRC_CMD_LOGGED_IN },
	{ "invite",      2, irc_cmd_invite,      IRC_CMD_LOGGED_IN },
	{ "privmsg",     1, irc_cmd_privmsg,     IRC_CMD_LOGGED_IN },
	{ "notice",      1, irc_cmd_privmsg,     IRC_CMD_LOGGED_IN },
	{ "who",         0, irc_cmd_who,         IRC_CMD_LOGGED_IN },
	{ "userhost",    1, irc_cmd_userhost,    IRC_CMD_LOGGED_IN },
	{ "ison",        1, irc_cmd_ison,        IRC_CMD_LOGGED_IN },
	{ "watch",       1, irc_cmd_watch,       IRC_CMD_LOGGED_IN },
	{ "topic",       1, irc_cmd_topic,       IRC_CMD_LOGGED_IN },
	{ "away",        0, irc_cmd_away,        IRC_CMD_LOGGED_IN },
	{ "whois",       1, irc_cmd_whois,       IRC_CMD_LOGGED_IN },
	{ "whowas",      1, irc_cmd_whowas,      IRC_CMD_LOGGED_IN },
	{ "nickserv",    1, irc_cmd_nickserv,    IRC_CMD_LOGGED_IN },
	{ "ns",          1, irc_cmd_nickserv,    IRC_CMD_LOGGED_IN },
	{ "motd",        0, irc_cmd_motd,        IRC_CMD_LOGGED_IN },
	{ "pong",        0, irc_cmd_pong,        IRC_CMD_LOGGED_IN },
	{ "completions", 0, irc_cmd_completions, IRC_CMD_LOGGED_IN },
	{ NULL }
};

int irc_exec( irc_t *irc, char *cmd[] )
{	
	int i, j;
	
	if( !cmd[0] )
		return( 1 );
	
	for( i = 0; irc_commands[i].command; i++ )
		if( g_strcasecmp( irc_commands[i].command, cmd[0] ) == 0 )
		{
			if( irc_commands[i].flags & IRC_CMD_PRE_LOGIN && irc->status >= USTATUS_LOGGED_IN )
			{
				irc_reply( irc, 462, ":Only allowed before logging in" );
				return( 1 );
			}
			if( irc_commands[i].flags & IRC_CMD_LOGGED_IN && irc->status < USTATUS_LOGGED_IN )
			{
				irc_reply( irc, 451, ":Register first" );
				return( 1 );
			}
			if( irc_commands[i].flags & IRC_CMD_OPER_ONLY && !strchr( irc->umode, 'o' ) )
			{
				irc_reply( irc, 481, ":Permission denied - You're not an IRC operator" );
				return( 1 );
			}
			
			for( j = 1; j <= irc_commands[i].required_parameters; j ++ )
				if( !cmd[j] )
				{
					irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
					return( 1 );
				}
			
			return irc_commands[i].execute( irc, cmd );
		}
	
	return( 1 );
}
