/* acct_r.c - Read accounting reply from server.
 * 
 * Copyright (C) 2010, Pawel Krawczyk <pawel.krawczyk@hush.com> and
 * Jeroen Nijhof <jeroen@nijhofnet.nl>
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
 * along with this program - see the file COPYING.
 *
 * See `CHANGES' file for revision history.
 */

#include "tacplus.h"
#include "xalloc.h"
#include "libtac.h"
#include "messages.h"

/*
 * return value:
 *   <  0 : error status code, see LIBTAC_STATUS_...
 *             LIBTAC_STATUS_READ_TIMEOUT
 *             LIBTAC_STATUS_SHORT_HDR
 *             LIBTAC_STATUS_SHORT_BODY
 *             LIBTAC_STATUS_PROTOCOL_ERR
 *   >= 0 : server response, see TAC_PLUS_AUTHEN_STATUS_...
 */
int tac_account_read(int fd, struct areply *re) {
    HDR th;
    struct acct_reply *tb = NULL;
    int len_from_header, r, len_from_body;
    char *msg = NULL;
    int timeleft;
    re->attr = NULL; /* unused */
    re->msg = NULL;

    if (tac_readtimeout_enable &&
        tac_read_wait(fd,tac_timeout*1000, TAC_PLUS_HDR_SIZE,&timeleft) < 0 ) {
        TACSYSLOG((LOG_ERR,\
            "%s: reply timeout after %d secs", __FUNCTION__, tac_timeout))
        re->msg = xstrdup(acct_syserr_msg);
        re->status = LIBTAC_STATUS_READ_TIMEOUT;
        free(tb);
        return re->status;
    }

    r=read(fd, &th, TAC_PLUS_HDR_SIZE);
    if(r < TAC_PLUS_HDR_SIZE) {
        TACSYSLOG((LOG_ERR,\
            "%s: short reply header, read %d of %d: %m", __FUNCTION__,\
            r, TAC_PLUS_HDR_SIZE))
        re->msg = xstrdup(acct_syserr_msg);
        re->status = LIBTAC_STATUS_SHORT_HDR;
        free(tb);
        return re->status;
    }

    /* check the reply fields in header */
    msg = _tac_check_header(&th, TAC_PLUS_ACCT);
    if(msg != NULL) {
        re->msg = xstrdup(msg);
        re->status = LIBTAC_STATUS_PROTOCOL_ERR;
        free(tb);
        TACDEBUG((LOG_DEBUG, "%s: exit status=%d, status message \"%s\"",\
            __FUNCTION__, re->status, re->msg != NULL ? re->msg : ""))
        return re->status;
    }

    len_from_header=ntohl(th.datalength);
    tb=(struct acct_reply *) xcalloc(1, len_from_header);

    /* read reply packet body */
    if (tac_readtimeout_enable &&
        tac_read_wait(fd,timeleft,len_from_header,NULL) < 0 ) {
        TACSYSLOG((LOG_ERR,\
            "%s: reply timeout after %d secs", __FUNCTION__, tac_timeout))
        re->msg = xstrdup(acct_syserr_msg);
        re->status = LIBTAC_STATUS_READ_TIMEOUT;
        free(tb);
        return re->status;
    }

    r=read(fd, tb, len_from_header);
    if(r < len_from_header) {
        TACSYSLOG((LOG_ERR,\
            "%s: short reply body, read %d of %d: %m",\
            __FUNCTION__,\
            r, len_from_header))
        re->msg = xstrdup(acct_syserr_msg);
        re->status = LIBTAC_STATUS_SHORT_BODY;
        free(tb);
        return re->status;
    }

    /* decrypt the body */
    _tac_crypt((u_char *) tb, &th, len_from_header);

    /* Convert network byte order to host byte order */
    tb->msg_len  = ntohs(tb->msg_len);
    tb->data_len = ntohs(tb->data_len);

    /* check the length fields */
    len_from_body=sizeof(tb->msg_len) + sizeof(tb->data_len) +
        sizeof(tb->status) + tb->msg_len + tb->data_len;

    if(len_from_header != len_from_body) {
        TACSYSLOG((LOG_ERR,\
            "%s: inconsistent reply body, incorrect key?",\
            __FUNCTION__))
        re->msg = xstrdup(acct_syserr_msg);
        re->status = LIBTAC_STATUS_PROTOCOL_ERR;
        free(tb);
        return re->status;
    }

    /* save status and clean up */
    r=tb->status;
    if(tb->msg_len) {
        msg=(char *) xcalloc(1, tb->msg_len+1);
        bcopy((u_char *) tb+TAC_ACCT_REPLY_FIXED_FIELDS_SIZE, msg, tb->msg_len); 
        msg[(int)tb->msg_len] = '\0';
        re->msg = msg;      // Freed by caller
    }

    /* server logged our request successfully */
    if (tb->status == TAC_PLUS_ACCT_STATUS_SUCCESS) {
        TACDEBUG((LOG_DEBUG, "%s: accounted ok", __FUNCTION__))
        if (!re->msg) re->msg = xstrdup(acct_ok_msg);
        re->status = tb->status;
        free(tb);
        return re->status;
    }

    TACDEBUG((LOG_DEBUG, "%s: accounting failed, server reply status=%d",\
        __FUNCTION__, tb->status))
    switch(tb->status) {
        case TAC_PLUS_ACCT_STATUS_FOLLOW:
            re->status = tb->status;
            if (!re->msg) re->msg=xstrdup(acct_fail_msg);
            break;

        case TAC_PLUS_ACCT_STATUS_ERROR:
        default:
            re->status = tb->status;
            if (!re->msg) re->msg=xstrdup(acct_err_msg);
            break;
    }

    free(tb);
    return re->status;
}
