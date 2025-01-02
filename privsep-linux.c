/*
 *
 *   Authors:
 *    Jim Paris			<jim@jtan.com>
 *    Pedro Roque		<roque@di.fc.ul.pt>
 *    Lars Fenneberg		<lf@elemental.net>
 *
 *   This software is Copyright 1996,1997,2008 by the above mentioned author(s),
 *   All Rights Reserved.
 *
 *   The license which is distributed with this software in the file COPYRIGHT
 *   applies to this software. If your distribution is missing this file, you
 *   may request it from <reubenhwk@gmail.com>.
 *
 */

#include "config.h"
#include "includes.h"
#include "pathnames.h"
#include "radvd.h"

static int set_interface_var(const char *iface, const char *var, const char *name, uint32_t val);
static void privsep_read_loop(void);

/* For reading or writing, depending on process */
static int pfd = -1;

void privsep_set_write_fd(int fd) { pfd = fd; }

/* Command types */
enum privsep_type {
	SET_INTERFACE_LINKMTU,
	SET_INTERFACE_CURHLIM,
	SET_INTERFACE_REACHTIME,
	SET_INTERFACE_RETRANSTIMER,
};

/* Command sent over pipe is a fixed size binary structure. */
struct privsep_command {
	int type;
	char iface[IFNAMSIZ];
	uint32_t val;
};

/* Privileged read loop */
static void privsep_read_loop(void)
{
	while (1) {
		struct privsep_command cmd;
		int ret = readn(pfd, &cmd, sizeof(cmd));
		if (ret <= 0) {
			/* Error or EOF, give up */
			if (ret < 0) {
				flog(LOG_ERR, "Exiting, privsep_read_loop had readn error: %s", strerror(errno));
			} else {
				flog(LOG_ERR, "Exiting, privsep_read_loop had readn return 0 bytes");
			}
		}
		if (ret != sizeof(cmd)) {
			/* Short read, ignore */
			return;
		}

		cmd.iface[IFNAMSIZ - 1] = '\0';

		switch (cmd.type) {

		case SET_INTERFACE_LINKMTU:
			if (cmd.val < MIN_AdvLinkMTU || cmd.val > MAX_AdvLinkMTU) {
				flog(LOG_ERR, "(privsep) %s: LinkMTU (%u) is not within the defined bounds, ignoring", cmd.iface,
				     cmd.val);
				break;
			}
			ret = set_interface_var(cmd.iface, PROC_SYS_IP6_LINKMTU, "LinkMTU", cmd.val);
			break;

		case SET_INTERFACE_CURHLIM:
			if (cmd.val < MIN_AdvCurHopLimit || cmd.val > MAX_AdvCurHopLimit) {
				flog(LOG_ERR, "(privsep) %s: CurHopLimit (%u) is not within the defined bounds, ignoring",
				     cmd.iface, cmd.val);
				break;
			}
			ret = set_interface_var(cmd.iface, PROC_SYS_IP6_CURHLIM, "CurHopLimit", cmd.val);
			break;

		case SET_INTERFACE_REACHTIME:
			if (cmd.val < MIN_AdvReachableTime || cmd.val > MAX_AdvReachableTime) {
				flog(LOG_ERR, "(privsep) %s: BaseReachableTimer (%u) is not within the defined bounds, ignoring",
				     cmd.iface, cmd.val);
				break;
			}
			ret = set_interface_var(cmd.iface, PROC_SYS_IP6_BASEREACHTIME_MS, "BaseReachableTimer (ms)", cmd.val);
			if (ret == 0)
				break;
			set_interface_var(cmd.iface, PROC_SYS_IP6_BASEREACHTIME, "BaseReachableTimer", cmd.val / 1000);
			break;

		case SET_INTERFACE_RETRANSTIMER:
			if (cmd.val < MIN_AdvRetransTimer || cmd.val > MAX_AdvRetransTimer) {
				flog(LOG_ERR, "(privsep) %s: RetransTimer (%u) is not within the defined bounds, ignoring",
				     cmd.iface, cmd.val);
				break;
			}
			ret = set_interface_var(cmd.iface, PROC_SYS_IP6_RETRANSTIMER_MS, "RetransTimer (ms)", cmd.val);
			if (ret == 0)
				break;
			set_interface_var(cmd.iface, PROC_SYS_IP6_RETRANSTIMER, "RetransTimer",
					  cmd.val / 1000 * USER_HZ); /* XXX user_hz */
			break;

		default:
			/* Bad command */
			break;
		}
	}
}

void privsep_init(int fd)
{
	/* This will be the privileged child */
	pfd = fd;
	privsep_read_loop();
	close(pfd);
	flog(LOG_ERR, "Exiting, privsep_read_loop is complete.");
}

/* Interface calls for the unprivileged process */
int privsep_interface_linkmtu(const char *iface, uint32_t mtu)
{
	struct privsep_command cmd;
	cmd.type = SET_INTERFACE_LINKMTU;
	memset(&cmd.iface, 0, sizeof(cmd.iface));
	strlcpy(cmd.iface, iface, sizeof(cmd.iface));
	cmd.val = mtu;

	if (writen(pfd, &cmd, sizeof(cmd)) != sizeof(cmd))
		return -1;
	return 0;
}

int privsep_interface_curhlim(const char *iface, uint32_t hlim)
{
	struct privsep_command cmd;
	cmd.type = SET_INTERFACE_CURHLIM;
	memset(&cmd.iface, 0, sizeof(cmd.iface));
	strlcpy(cmd.iface, iface, sizeof(cmd.iface));
	cmd.val = hlim;
	if (writen(pfd, &cmd, sizeof(cmd)) != sizeof(cmd))
		return -1;
	return 0;
}

int privsep_interface_reachtime(const char *iface, uint32_t rtime)
{
	struct privsep_command cmd;
	cmd.type = SET_INTERFACE_REACHTIME;
	memset(&cmd.iface, 0, sizeof(cmd.iface));
	strlcpy(cmd.iface, iface, sizeof(cmd.iface));
	cmd.val = rtime;
	if (writen(pfd, &cmd, sizeof(cmd)) != sizeof(cmd))
		return -1;
	return 0;
}

int privsep_interface_retranstimer(const char *iface, uint32_t rettimer)
{
	struct privsep_command cmd;
	cmd.type = SET_INTERFACE_RETRANSTIMER;
	memset(&cmd.iface, 0, sizeof(cmd.iface));
	strlcpy(cmd.iface, iface, sizeof(cmd.iface));
	cmd.val = rettimer;
	if (writen(pfd, &cmd, sizeof(cmd)) != sizeof(cmd))
		return -1;
	return 0;
}

/* note: also called from the root context */
static int set_interface_var(const char *iface, const char *var, const char *name, uint32_t val)
{
	int retval = -1;
	FILE *fp = 0;
	char *spath = strdupf(var, iface);
	int fd = -1 ;

	/* No path traversal */
	// TODO: if interface names contain '/' in future, this may break.
	if (!iface[0] || !strcmp(iface, ".") || !strcmp(iface, "..") || strchr(iface, '/'))
		goto errmsg;

	// Open the file for writing, ONLY if it already exists.
	// explicitly ensure that O_CREAT is *NOT* set.
	fd = open(spath, O_WRONLY & (~O_CREAT), 0);
	if (fd == -1) {
		// If the file does NOT exist, this is non-fatal; as we fallback to a
		// different filename at a higher level.
		// ensure we cleanup and return -1 to the higher level
		if (errno == ENOENT)
			goto cleanup;
		// If we got some other error, e.g. ENOACCESS
		goto errmsg;
	}

	// We know the file exists now, write to it.
	fp = fdopen(fd, "w");
	if (!fp)
		goto errmsg;

	if (fprintf(fp, "%u", val) > 0) {
		retval = 0;
	}

errmsg:
	if (name && retval != 0)
		flog(LOG_ERR, "failed to set %s (%u) for %s: %s", name, val, iface, strerror(errno));
cleanup:

	if (fp)
		fclose(fp);
	if (fd != -1)
		close(fd);

	free(spath);

	return retval;
}
