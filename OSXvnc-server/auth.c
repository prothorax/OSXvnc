/*
 * auth.c - deal with authentication.
 *
 * This file implements the VNC authentication protocol when setting up an RFB
 * connection.
 */

/*
 *  OSXvnc Copyright (C) 2001 Dan McGuirk <mcguirk@incompleteness.net>.
 *  Original Xvnc code Copyright (C) 1999 AT&T Laboratories Cambridge.  
 *  All Rights Reserved.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include "rfb.h"


char *rfbAuthPasswdFile = NULL;


/*
 * rfbAuthNewClient is called when we reach the point of authenticating
 * a new client.  If authentication isn't being used then we simply send
 * rfbNoAuth.  Otherwise we send rfbVncAuth plus the challenge.
 */

void rfbAuthNewClient(rfbClientPtr cl) {
    char buf[4 + CHALLENGESIZE];
    int len = 0;

    if (0 && cl->minor >= 7) { // This doesn't seem to behave as documented
        buf[len++] = 1; // 1 Type
        
        if (rfbAuthPasswdFile && !cl->reverseConnection && (vncDecryptPasswdFromFile(rfbAuthPasswdFile) != NULL)) {
            buf[len++] = rfbVncAuth;
            cl->state = RFB_AUTH_VERSION;
        }
        else {
            buf[len++] = rfbNoAuth;
            cl->state = RFB_INITIALISATION;
        }

        if (WriteExact(cl, buf, len) < 0) {
            rfbLogPerror("rfbAuthNewClient: write");
            rfbCloseClient(cl);
            return;
        }
    }
    else {
        // If We have a valid password - Send Challenge Request
        if (rfbAuthPasswdFile && !cl->reverseConnection && (vncDecryptPasswdFromFile(rfbAuthPasswdFile) != NULL)) {
            *(CARD32 *)buf = Swap32IfLE(rfbVncAuth);
            vncRandomBytes(cl->authChallenge);
            memcpy(&buf[4], (char *)cl->authChallenge, CHALLENGESIZE);
            len = 4 + CHALLENGESIZE;

            cl->state = RFB_AUTHENTICATION;
        }
        // Otherwise just send NO auth
        else {
            *(CARD32 *)buf = Swap32IfLE(rfbNoAuth);
            len = 4;

            cl->state = RFB_INITIALISATION;
        }

        if (WriteExact(cl, buf, len) < 0) {
            rfbLogPerror("rfbAuthNewClient: write");
            rfbCloseClient(cl);
            return;
        }
    }
}

void rfbProcessAuthVersion(rfbClientPtr cl) {
    int n;
    CARD8 securityType;

    if ((n = ReadExact(cl, &securityType, 1)) <= 0) {
        if (n != 0)
            rfbLogPerror("rfbProcessAuthVersion: read");
        rfbCloseClient(cl);
        return;
    }

    switch (securityType) {
        case rfbVncAuth: {
            char buf[CHALLENGESIZE];
            int len = 0;

            vncRandomBytes(cl->authChallenge);
            memcpy(buf, (char *)cl->authChallenge, CHALLENGESIZE);
            len = CHALLENGESIZE;

            if (WriteExact(cl, buf, len) < 0) {
                rfbLogPerror("rfbProcessAuthVersion: write");
                rfbCloseClient(cl);
                return;
            }

            cl->state = RFB_AUTHENTICATION;
            break;
        }
        default:
            rfbLogPerror("rfbProcessAuthVersion: Invalid Authorization Type");
            rfbCloseClient(cl);
            break;
    }
}

/*
 * rfbAuthProcessClientMessage is called when the client sends its
 * authentication response.
 */

void rfbAuthProcessClientMessage(rfbClientPtr cl) {
    char *passwd;
    int i, n;
    CARD8 response[CHALLENGESIZE];
    CARD32 authResult;

    if ((n = ReadExact(cl, (char *)response, CHALLENGESIZE)) <= 0) {
        if (n != 0)
            rfbLogPerror("rfbAuthProcessClientMessage: read");
        rfbCloseClient(cl);
        return;
    }

    passwd = vncDecryptPasswdFromFile(rfbAuthPasswdFile);

    if (passwd == NULL) {
        rfbLog("rfbAuthProcessClientMessage: could not get password from %s\n",
               rfbAuthPasswdFile);

        authResult = Swap32IfLE(rfbVncAuthFailed);

        if (WriteExact(cl, (char *)&authResult, 4) < 0) {
            rfbLogPerror("rfbAuthProcessClientMessage: write");
        }
        rfbCloseClient(cl);
        return;
    }

    vncEncryptBytes(cl->authChallenge, passwd);

    /* Lose the password from memory */
    for (i = strlen(passwd); i >= 0; i--) {
        passwd[i] = '\0';
    }

    free((char *)passwd);

    if (memcmp(cl->authChallenge, response, CHALLENGESIZE) != 0) {
        rfbLog("rfbAuthProcessClientMessage: authentication failed from %s\n",
               cl->host);

        authResult = Swap32IfLE(rfbVncAuthFailed);

        if (WriteExact(cl, (char *)&authResult, 4) < 0) {
            rfbLogPerror("rfbAuthProcessClientMessage: write");
        }
        rfbCloseClient(cl);
        return;
    }

    authResult = Swap32IfLE(rfbVncAuthOK);

    if (WriteExact(cl, (char *)&authResult, 4) < 0) {
        rfbLogPerror("rfbAuthProcessClientMessage: write");
        rfbCloseClient(cl);
        return;
    }

    cl->state = RFB_INITIALISATION;
}
