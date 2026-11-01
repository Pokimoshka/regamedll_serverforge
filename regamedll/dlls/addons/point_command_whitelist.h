/*
*
*   This program is free software; you can redistribute it and/or modify it
*   under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or (at
*   your option) any later version.
*
*/

#pragma once

//-----------------------------------------------------------------------------
// Whitelist команд, разрешённых для point_servercommand / point_clientcommand.
// По умолчанию разблокированы только безвредные игровые cvar.
// Любая команда, не входящая в список, блокируется и пишется в лог.
// Дополнительные команды можно разрешить через cvar mp_point_servercommand_whitelist.
//-----------------------------------------------------------------------------
static const char *g_allowedServerCommands[] =
{
	// безопасные игровые настройки, которые карты обычно меняют
	"mp_roundtime",
	"mp_freezetime",
	"mp_buytime",
	"mp_c4timer",
	"sv_gravity",
	"sv_friction",
	"sv_stepsize",
	"sv_maxspeed",
	"sv_stopspeed",
	"sv_accelerate",
	"sv_airaccelerate",
	"sv_wateraccelerate",
	"sv_waterfriction",
	nullptr
};

//-----------------------------------------------------------------------------
// Проверка на опасные символы в аргументе команды (защита от инъекций).
// Возвращает true, если строка содержит символы, позволяющие выполнить
// цепочку команд или внедрить аргументы.
//-----------------------------------------------------------------------------
inline bool CommandContainsDangerousChars(const char *cmd)
{
	if (!cmd)
		return true;

	for (; *cmd; ++cmd)
	{
		const char c = *cmd;
		if (c == ';' || c == '|' || c == '&' || c == '`' || c == '$' ||
			c == '<' || c == '>' || c == '(' || c == ')' || c == '{' || c == '}' ||
			c == '!' || c == '\n' || c == '\r')
		{
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Регистронезависимая проверка имени команды против встроенного whitelist.
//-----------------------------------------------------------------------------
inline bool IsBuiltinCommandAllowed(const char *cmdName)
{
	if (!cmdName || cmdName[0] == '\0')
		return false;

	for (int i = 0; g_allowedServerCommands[i] != nullptr; ++i)
	{
		if (Q_stricmp(cmdName, g_allowedServerCommands[i]) == 0)
			return true;
	}

	return false;
}
