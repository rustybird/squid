/*
 * DEBUG: section 28    Access Control
 * AUTHOR: Duane Wessels
 *
 * This file contains ACL routines that are not part of the
 * ACL class, nor any other class yet, and that need to be
 * factored into appropriate places. They are here to reduce
 * unneeded dependencies between the ACL class and the rest
 * of squid.
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#include "squid.h"
#include "acl/Acl.h"
#include "acl/AclNameList.h"
#include "acl/AclDenyInfoList.h"
#include "acl/Checklist.h"
#include "acl/Tree.h"
#include "acl/Strategised.h"
#include "acl/Gadgets.h"
#include "ConfigParser.h"
#include "errorpage.h"
#include "globals.h"
#include "HttpRequest.h"
#include "Mem.h"

/* does name lookup, returns page_id */
err_type
aclGetDenyInfoPage(AclDenyInfoList ** head, const char *name, int redirect_allowed)
{
    if (!name) {
        debugs(28, 3, "ERR_NONE due to a NULL name");
        return ERR_NONE;
    }

    AclDenyInfoList *A = NULL;

    debugs(28, 8, HERE << "got called for " << name);

    for (A = *head; A; A = A->next) {
        AclNameList *L = NULL;

        if (!redirect_allowed && strchr(A->err_page_name, ':') ) {
            debugs(28, 8, HERE << "Skip '" << A->err_page_name << "' 30x redirects not allowed as response here.");
            continue;
        }

        for (L = A->acl_list; L; L = L->next) {
            if (!strcmp(name, L->name)) {
                debugs(28, 8, HERE << "match on " << name);
                return A->err_page_id;
            }

        }
    }

    debugs(28, 8, "aclGetDenyInfoPage: no match");
    return ERR_NONE;
}

/* does name lookup, returns if it is a proxy_auth acl */
int
aclIsProxyAuth(const char *name)
{
    if (!name) {
        debugs(28, 3, "false due to a NULL name");
        return false;
    }

    debugs(28, 5, "aclIsProxyAuth: called for " << name);

    ACL *a;

    if ((a = ACL::FindByName(name))) {
        debugs(28, 5, "aclIsProxyAuth: returning " << a->isProxyAuth());
        return a->isProxyAuth();
    }

    debugs(28, 3, "aclIsProxyAuth: WARNING, called for nonexistent ACL");
    return false;
}

/* maex@space.net (05.09.96)
 *    get the info for redirecting "access denied" to info pages
 *      TODO (probably ;-)
 *      currently there is no optimization for
 *      - more than one deny_info line with the same url
 *      - a check, whether the given acl really is defined
 *      - a check, whether an acl is added more than once for the same url
 */

void
aclParseDenyInfoLine(AclDenyInfoList ** head)
{
    char *t = NULL;
    AclDenyInfoList *A = NULL;
    AclDenyInfoList *B = NULL;
    AclDenyInfoList **T = NULL;
    AclNameList *L = NULL;
    AclNameList **Tail = NULL;

    /* first expect a page name */

    if ((t = strtok(NULL, w_space)) == NULL) {
        debugs(28, DBG_CRITICAL, "aclParseDenyInfoLine: " << cfg_filename << " line " << config_lineno << ": " << config_input_line);
        debugs(28, DBG_CRITICAL, "aclParseDenyInfoLine: missing 'error page' parameter.");
        return;
    }

    A = (AclDenyInfoList *)memAllocate(MEM_ACL_DENY_INFO_LIST);
    A->err_page_id = errorReservePageId(t);
    A->err_page_name = xstrdup(t);
    A->next = (AclDenyInfoList *) NULL;
    /* next expect a list of ACL names */
    Tail = &A->acl_list;

    while ((t = strtok(NULL, w_space))) {
        L = (AclNameList *)memAllocate(MEM_ACL_NAME_LIST);
        xstrncpy(L->name, t, ACL_NAME_SZ-1);
        *Tail = L;
        Tail = &L->next;
    }

    if (A->acl_list == NULL) {
        debugs(28, DBG_CRITICAL, "aclParseDenyInfoLine: " << cfg_filename << " line " << config_lineno << ": " << config_input_line);
        debugs(28, DBG_CRITICAL, "aclParseDenyInfoLine: deny_info line contains no ACL's, skipping");
        memFree(A, MEM_ACL_DENY_INFO_LIST);
        return;
    }

    for (B = *head, T = head; B; T = &B->next, B = B->next)

        ;	/* find the tail */
    *T = A;
}

void
aclParseAccessLine(const char *directive, ConfigParser &, acl_access **treep)
{
    /* first expect either 'allow' or 'deny' */
    const char *t = ConfigParser::strtokFile();

    if (!t) {
        debugs(28, DBG_CRITICAL, "aclParseAccessLine: " << cfg_filename << " line " << config_lineno << ": " << config_input_line);
        debugs(28, DBG_CRITICAL, "aclParseAccessLine: missing 'allow' or 'deny'.");
        return;
    }

    allow_t action = ACCESS_DUNNO;
    if (!strcmp(t, "allow"))
        action = ACCESS_ALLOWED;
    else if (!strcmp(t, "deny"))
        action = ACCESS_DENIED;
    else {
        debugs(28, DBG_CRITICAL, "aclParseAccessLine: " << cfg_filename << " line " << config_lineno << ": " << config_input_line);
        debugs(28, DBG_CRITICAL, "aclParseAccessLine: expecting 'allow' or 'deny', got '" << t << "'.");
        return;
    }

    const int ruleId = ((treep && *treep) ? (*treep)->childrenCount() : 0) + 1;
    MemBuf ctxBuf;
    ctxBuf.init();
    ctxBuf.Printf("%s#%d", directive, ruleId);
    ctxBuf.terminate();

    Acl::AndNode *rule = new Acl::AndNode;
    rule->context(ctxBuf.content(), config_input_line);
    rule->lineParse();
    if (rule->empty()) {
        debugs(28, DBG_CRITICAL, "aclParseAccessLine: " << cfg_filename << " line " << config_lineno << ": " << config_input_line);
        debugs(28, DBG_CRITICAL, "aclParseAccessLine: Access line contains no ACL's, skipping");
        delete rule;
        return;
    }

    /* Append to the end of this list */

    assert(treep);
    if (!*treep) {
        *treep = new Acl::Tree;
        (*treep)->context(directive, config_input_line);
    }

    (*treep)->add(rule, action);

    /* We lock _acl_access structures in ACLChecklist::matchNonBlocking() */
}

// aclParseAclList does not expect or set actions (cf. aclParseAccessLine)
void
aclParseAclList(ConfigParser &, Acl::Tree **treep, const char *label)
{
    // accomodate callers unable to convert their ACL list context to string
    if (!label)
        label = "...";

    MemBuf ctxLine;
    ctxLine.init();
    ctxLine.Printf("(%s %s line)", cfg_directive, label);
    ctxLine.terminate();

    Acl::AndNode *rule = new Acl::AndNode;
    rule->context(ctxLine.content(), config_input_line);
    rule->lineParse();

    MemBuf ctxTree;
    ctxTree.init();
    ctxTree.Printf("%s %s", cfg_directive, label);
    ctxTree.terminate();

    // We want a cbdata-protected Tree (despite giving it only one child node).
    Acl::Tree *tree = new Acl::Tree;
    tree->add(rule);
    tree->context(ctxTree.content(), config_input_line);

    assert(treep);
    assert(!*treep);
    *treep = tree;
}

/*********************/
/* Destroy functions */
/*********************/

void
aclDestroyAcls(ACL ** head)
{
    ACL *next = NULL;

    debugs(28, 8, "aclDestroyACLs: invoked");

    for (ACL *a = *head; a; a = next) {
        next = a->next;
        delete a;
    }

    *head = NULL;
}

void
aclDestroyAclList(ACLList **list)
{
    debugs(28, 8, "aclDestroyAclList: invoked");
    assert(list);
    cbdataFree(*list);
}

void
aclDestroyAccessList(acl_access ** list)
{
    assert(list);
    if (*list)
        debugs(28, 3, "destroying: " << *list << ' ' << (*list)->name);
    cbdataFree(*list);
}

/* maex@space.net (06.09.1996)
 *    destroy an AclDenyInfoList */

void
aclDestroyDenyInfoList(AclDenyInfoList ** list)
{
    AclDenyInfoList *a = NULL;
    AclDenyInfoList *a_next = NULL;
    AclNameList *l = NULL;
    AclNameList *l_next = NULL;

    debugs(28, 8, "aclDestroyDenyInfoList: invoked");

    for (a = *list; a; a = a_next) {
        for (l = a->acl_list; l; l = l_next) {
            l_next = l->next;
            safe_free(l);
        }

        a_next = a->next;
        xfree(a->err_page_name);
        memFree(a, MEM_ACL_DENY_INFO_LIST);
    }

    *list = NULL;
}
