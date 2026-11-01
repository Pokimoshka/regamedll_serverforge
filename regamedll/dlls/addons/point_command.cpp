/*
*
*   This program is free software; you can redistribute it and/or modify it
*   under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or (at
*   your option) any later version.
*
*   This program is distributed in the hope that it will be useful, but
*   WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*   General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software Foundation,
*   Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*/

#include "precompiled.h"
#include "addons/point_command_whitelist.h"

//-----------------------------------------------------------------------------
// Проверка команды против whitelist (регистронезависимо).
// Проверяется встроенный список безопасных команд + расширяемый через cvar
// mp_point_servercommand_whitelist (разделители: пробел, запятая, табуляция).
//-----------------------------------------------------------------------------
static bool IsCommandAllowed(const char *cmdName)
{
	if (!cmdName || cmdName[0] == '\0')
		return false;

	// 1. встроенный whitelist безопасных команд
	if (IsBuiltinCommandAllowed(cmdName))
		return true;

	// 2. расширяемый whitelist из cvar
	const char *list = point_servercommand_whitelist.string;
	if (list && list[0] != '\0')
	{
		const char *p = list;
		while (*p)
		{
			// пропускаем разделители
			while (*p == ' ' || *p == ',' || *p == '\t')
				p++;
			if (*p == '\0')
				break;

			// находим конец токена
			const char *end = p;
			while (*end && *end != ' ' && *end != ',' && *end != '\t')
				end++;

			const int tokenLen = end - p;
			if (tokenLen > 0 && (int)Q_strlen(cmdName) == tokenLen && Q_strnicmp(cmdName, p, tokenLen) == 0)
				return true;

			p = end;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Логирование заблокированной попытки выполнить команду через point_*
//-----------------------------------------------------------------------------
static void LogBlockedCommand(const char *entityClass, const char *cmdName, const char *cmdValue)
{
	ALERT(at_warning, "point_command blocked unsafe command: entity=%s cmd=\"%s\" value=\"%s\"\n",
		entityClass ? entityClass : "unknown",
		cmdName ? cmdName : "null",
		cmdValue ? cmdValue : "null");
}

void CPointBaseCommand::KeyValue(KeyValueData *pkvd)
{
	// add this field to the command list
	if (m_vecCommands.Count() < MAX_POINT_CMDS)
	{
		if (pkvd->szValue[0] != '\0' &&
			Q_strcmp(pkvd->szValue, "-") != 0)
		{
			m_vecCommands.AddToTail({ pkvd->szKeyName, pkvd->szValue });
		}
		else
		{
			m_vecCommands.AddToTail(pkvd->szKeyName);
		}

		pkvd->fHandled = TRUE;
		return;
	}

	CPointEntity::KeyValue(pkvd);
}

LINK_ENTITY_TO_CLASS(point_clientcommand, CPointClientCommand, CCSPointClientCommand)

void CPointClientCommand::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	edict_t *pClient = nullptr;
	if (gpGlobals->maxClients == 1)
	{
		pClient = INDEXENT(1);
	}
	else if (pActivator &&
		pActivator->IsPlayer() &&
		pActivator->IsNetClient() &&
		!pActivator->IsDormant())
	{
		// In multiplayer, send it back to the activator
		pClient = pActivator->edict();
	}

	if (pClient)
	{
		for (auto &cmd : m_vecCommands) {
			if (cmd.name[0] == '\0')
				continue;

			if (!IsCommandAllowed(cmd.name)) {
				LogBlockedCommand("point_clientcommand", cmd.name, cmd.value);
				continue;
			}

			if (cmd.value && CommandContainsDangerousChars(cmd.value)) {
				LogBlockedCommand("point_clientcommand", cmd.name, cmd.value);
				continue;
			}

			Execute(pClient, "%s \"%s\"\n", cmd.name, cmd.value);
		}
	}
}

void CPointClientCommand::Execute(edict_t *pEdict, const char *pszFmt, ...)
{
	va_list argptr;
	char command[128];

	va_start(argptr, pszFmt);
	Q_vsnprintf(command, sizeof(command), pszFmt, argptr);
	va_end(argptr);

	CLIENT_COMMAND(pEdict, command);
}

LINK_ENTITY_TO_CLASS(point_servercommand, CPointServerCommand, CCSPointServerCommand)

void CPointServerCommand::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
#ifdef REGAMEDLL_ADD
	if (!allow_point_servercommand.value) {
		ALERT(at_warning, "point_servercommand usage blocked by mp_allow_point_servercommand setting\n");
		return;
	}
#endif

	for (auto &cmd : m_vecCommands)
	{
		if (cmd.name[0] == '\0')
			continue;

		// whitelist: выполняем только разрешённые команды
		if (!IsCommandAllowed(cmd.name)) {
			LogBlockedCommand("point_servercommand", cmd.name, cmd.value);
			continue;
		}

		// защита от инъекций в значении
		if (cmd.value && CommandContainsDangerousChars(cmd.value)) {
			LogBlockedCommand("point_servercommand", cmd.name, cmd.value);
			continue;
		}

		// запоминаем исходное значение cvar для сброса при удалении сущности
		cvar_t *pCVar = CVAR_GET_POINTER(cmd.name);
		if (pCVar &&
			pCVar->string &&
			pCVar->string[0] != '\0' &&
			Q_stricmp(cmd.value, pCVar->string) != 0) {
			Q_strlcpy(cmd.valueInitial, pCVar->string);
		}

		if (cmd.value[0] != '\0')
		{
			Execute(nullptr, "%s \"%s\"\n", cmd.name, cmd.value);
		}
		else
		{
			Execute(nullptr, "%s\n", cmd.name);
		}
	}
}

void CPointServerCommand::Execute(edict_t *pEdict, const char *pszFmt, ...)
{
	va_list argptr;
	char command[128];

	va_start(argptr, pszFmt);
	Q_vsnprintf(command, sizeof(command), pszFmt, argptr);
	va_end(argptr);

	if (!IS_DEDICATED_SERVER())
	{
		// На не-выделенном сервере НЕ отправляем произвольные команды клиенту.
		// Это потенциально dangereux: недоверенная карта может выполнить что угодно на стороне клиента.
		// Разрешаем только whitelist-команды, и то через серверный SERVER_COMMAND.
		ALERT(at_warning, "point_servercommand: blocked client-side execution on non-dedicated server: %s\n", command);
		return;
	}

	SERVER_COMMAND(command);
}

void CPointBaseCommand::OnDestroy()
{
	if (!(pev->spawnflags & SF_POINT_CMD_NORESET))
	{
		bool bAtLeastOneCmdReset = false;
		for (auto &cmd : m_vecCommands)
		{
			if (cmd.valueInitial[0] != '\0')
			{
				// сбрасываем только если команда в whitelist
				if (IsCommandAllowed(cmd.name))
				{
					Execute(nullptr, "%s \"%s\"\n", cmd.name, cmd.valueInitial);
					bAtLeastOneCmdReset = true;
				}
			}
		}

		if (bAtLeastOneCmdReset)
		{
			SERVER_EXECUTE();
		}
	}

	m_vecCommands.Purge();
}