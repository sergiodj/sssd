/*
   SSSD

   System Database

   Copyright (C) Stephen Gallagher <sgallagh@redhat.com>	2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <check.h>
#include <talloc.h>
#include <tevent.h>
#include <popt.h>
#include "util/util.h"
#include "confdb/confdb.h"
#include "db/sysdb.h"

#define SYSDB_POSIX_TEST_GROUP "sysdbtestgroup"

struct sysdb_test_ctx {
    struct sysdb_ctx *sysdb;
    struct confdb_ctx *confdb;
    struct event_context *ev;
};

static int setup_sysdb_tests(struct sysdb_test_ctx **ctx)
{
    struct sysdb_test_ctx *test_ctx;
    int ret;

    test_ctx = talloc_zero(NULL, struct sysdb_test_ctx);
    if (test_ctx == NULL) {
        fail("Could not allocate memory for test context");
        return ENOMEM;
    }

    /* Create an event context
     * It will not be used except in confdb_init and sysdb_init
     */
    test_ctx->ev = event_context_init(test_ctx);
    if (test_ctx->ev == NULL) {
        fail("Could not create event context");
        talloc_free(test_ctx);
        return EIO;
    }

    /* Connect to the conf db */
    ret = confdb_init(test_ctx, test_ctx->ev, &test_ctx->confdb);
    if(ret != EOK) {
        fail("Could not initialize connection to the confdb");
        talloc_free(test_ctx);
        return ret;
    }

    ret = sysdb_init(test_ctx, test_ctx->ev, test_ctx->confdb, &test_ctx->sysdb);
    if(ret != EOK) {
        fail("Could not initialize connection to the sysdb");
        talloc_free(test_ctx);
        return ret;
    }

    *ctx = test_ctx;
    return EOK;
}

START_TEST (test_sysdb_store_local_account_posix)
{
    int ret;
    struct sysdb_test_ctx *test_ctx;

    /* Setup */
    ret = setup_sysdb_tests(&test_ctx);
    if (ret != EOK) {
        fail("Could not set up the test");
        return;
    }

    /* Store a user account with username, password,
     * uid, gid, gecos, homedir and shell
     */
    const char *username = talloc_asprintf(test_ctx, "testuser%d", _i);
    const char *home = talloc_asprintf(test_ctx, "/home/testuser%d", _i);

    ret = sysdb_store_account_posix(test_ctx, test_ctx->sysdb,
                            "LOCAL", username, "password",
                            _i, _i,
                            "Test User",
                            home,
                            "/bin/bash");
    fail_if(ret != EOK, "Could not store POSIX user %s", username);

    talloc_free(test_ctx);
}
END_TEST

START_TEST (test_sysdb_store_local_group_posix)
{
    int ret;
    struct sysdb_test_ctx *test_ctx;

    /* Setup */
    ret = setup_sysdb_tests(&test_ctx);
    if (ret != EOK) {
        fail("Could not set up the test");
        return;
    }

    ret = sysdb_store_group_posix(test_ctx, test_ctx->sysdb,
                            "LOCAL", SYSDB_POSIX_TEST_GROUP, _i);
    fail_if(ret != EOK, "Could not store POSIX group");

    talloc_free(test_ctx);
}
END_TEST

START_TEST (test_sysdb_get_local_group_posix)
{
    int ret;
    struct sysdb_test_ctx *test_ctx;
    struct ldb_result *res;
    struct ldb_dn *base_group_dn;
    const char *attrs[] = { SYSDB_GR_NAME, SYSDB_GR_GIDNUM, NULL };
    const char *name;
    gid_t test_gid;

    /* Setup */
    ret = setup_sysdb_tests(&test_ctx);
    if (ret != EOK) {
        fail("Could not set up the test");
        return;
    }

    /* Set up the base DN */
    base_group_dn = ldb_dn_new_fmt(test_ctx, test_ctx->sysdb->ldb,
                                   SYSDB_TMPL_GROUP_BASE, "LOCAL");
    if (base_group_dn == NULL) {
        fail("Could not create basedn for LOCAL groups");
        return;
    }

    /* Look up the group by gid */
    ret = ldb_search(test_ctx->sysdb->ldb, test_ctx,
                     &res, base_group_dn, LDB_SCOPE_ONELEVEL,
                     attrs, SYSDB_GRGID_FILTER, (unsigned long)_i);
    if (ret != LDB_SUCCESS) {
        fail("Could not locate group %d", _i);
        return;
    }

    if (res->count < 1) {
        fail("Local group %d doesn't exist.\n", _i);
        return;
    }
    else if (res->count > 1) {
        fail("More than one group shared gid %d", _i);
        return;
    }

    name = ldb_msg_find_attr_as_string(res->msgs[0], SYSDB_GR_NAME, NULL);
    fail_unless(strcmp(name, SYSDB_POSIX_TEST_GROUP) == 0,
                "Returned group name was %s, expecting %s",
                name, SYSDB_POSIX_TEST_GROUP);
    talloc_free(res);

    /* Look up the group by name */
    ret = ldb_search(test_ctx->sysdb->ldb, test_ctx,
                     &res, base_group_dn, LDB_SCOPE_ONELEVEL,
                     attrs, SYSDB_GRNAM_FILTER, SYSDB_POSIX_TEST_GROUP);
    if (ret != LDB_SUCCESS) {
        fail("Could not locate group %d", _i);
        return;
    }

    if (res->count < 1) {
        fail("Local group %s doesn't exist.", SYSDB_POSIX_TEST_GROUP);
        return;
    }
    else if (res->count > 1) {
        fail("More than one group shared name %s", SYSDB_POSIX_TEST_GROUP);
        return;
    }

    test_gid = ldb_msg_find_attr_as_uint64(res->msgs[0], SYSDB_GR_GIDNUM, 0);
    fail_unless(test_gid == _i,
                "Returned group id was %lu, expecting %lu",
                test_gid, _i);

    talloc_free(test_ctx);
}
END_TEST

START_TEST (test_sysdb_add_acct_to_posix_group)
{
    int ret;
    struct sysdb_test_ctx *test_ctx;
    char *username;

    /* Setup */
    ret = setup_sysdb_tests(&test_ctx);
    if (ret != EOK) {
        fail("Could not set up the test");
        return;
    }

    /* Add user to test group */
    username = talloc_asprintf(test_ctx, "testuser%d", _i);
    ret = sysdb_add_acct_to_posix_group(test_ctx,
                                        test_ctx->sysdb,
                                        "LOCAL",
                                        SYSDB_POSIX_TEST_GROUP,
                                        username);
    fail_if(ret != EOK,
            "Failed to add user %s to group %s. Error was: %d",
            username, SYSDB_POSIX_TEST_GROUP, ret);

    talloc_free(test_ctx);
}
END_TEST

START_TEST (test_sysdb_verify_posix_group_members)
{
    char found_group, found_user;
    int ret, i;
    struct sysdb_test_ctx *test_ctx;
    char *username;
    char *member;
    char *group;
    struct ldb_dn *group_dn;
    struct ldb_dn *user_dn;
    struct ldb_result *res;
    struct ldb_message_element *el;
    const char *group_attrs[] = { SYSDB_GR_MEMBER, NULL };
    const char *user_attrs[] = { SYSDB_PW_MEMBEROF, NULL };

    /* Setup */
    ret = setup_sysdb_tests(&test_ctx);
    if (ret != EOK) {
        fail("Could not set up the test");
        return;
    }

    username = talloc_asprintf(test_ctx, "testuser%d", _i);
    fail_if (username == NULL, "Could not allocate username");

    member = talloc_asprintf(test_ctx,
                             SYSDB_PW_NAME"=%s,"SYSDB_TMPL_USER_BASE,
                             username, "LOCAL");
    fail_if(member == NULL, "Could not allocate member dn");

    user_dn = ldb_dn_new_fmt(test_ctx, test_ctx->sysdb->ldb, member);
    fail_if(user_dn == NULL, "Could not create user_dn object");

    group = talloc_asprintf(test_ctx,
                            SYSDB_GR_NAME"=%s,"SYSDB_TMPL_GROUP_BASE,
                            SYSDB_POSIX_TEST_GROUP, "LOCAL");
    fail_if(group == NULL, "Could not allocate group dn");

    group_dn = ldb_dn_new_fmt(test_ctx, test_ctx->sysdb->ldb, group);
    fail_if(group_dn == NULL, "Could not create group_dn object");

    /* Look up the group by name */
    ret = ldb_search(test_ctx->sysdb->ldb, test_ctx,
                     &res, group_dn, LDB_SCOPE_BASE,
                     group_attrs, SYSDB_GRNAM_FILTER, SYSDB_POSIX_TEST_GROUP);
    if (ret != LDB_SUCCESS) {
        fail("Could not locate group %d", _i);
        return;
    }

    if (res->count < 1) {
        fail("Local group %s doesn't exist.", SYSDB_POSIX_TEST_GROUP);
        return;
    }
    else if (res->count > 1) {
        fail("More than one group shared name %s", SYSDB_POSIX_TEST_GROUP);
        return;
    }

    /* Check the members for the requested user */
    found_group = i = 0;
    el = ldb_msg_find_element(res->msgs[0], SYSDB_GR_MEMBER);
    if (el && el->num_values > 0) {
        while (i < el->num_values && !found_group) {
            struct ldb_val v = el->values[i];
            char *value = talloc_strndup(test_ctx, (char *)v.data, v.length);
            if (strcmp(value, member) == 0) {
                found_group = 1;
            }
            talloc_free(value);
            i++;
        }
    }
    else {
        fail("No member attributes for group %s", SYSDB_POSIX_TEST_GROUP);
    }

    fail_unless(found_group == 1, "%s does not have %s as a member", SYSDB_POSIX_TEST_GROUP, username);

    /* Look up the user by name */
    ret = ldb_search(test_ctx->sysdb->ldb, test_ctx,
                     &res, user_dn, LDB_SCOPE_BASE,
                     user_attrs, SYSDB_PWNAM_FILTER, username);
    if (ret != LDB_SUCCESS) {
        fail("Could not locate user %s", username);
        return;
    }

    if (res->count < 1) {
        fail("Local user %s doesn't exist.", username);
        return;
    }
    else if (res->count > 1) {
        fail("More than one user shared name %s", username);
        return;
    }

    /* Check that the user is a member of the SYSDB_POSIX_TEST_GROUP */
    found_user = i = 0;
    el = ldb_msg_find_element(res->msgs[0], SYSDB_PW_MEMBEROF);
    if (el && el->num_values > 0) {
        while (i < el->num_values && !found_user) {
            struct ldb_val v = el->values[i];
            char *value = talloc_strndup(test_ctx, (char *)v.data, v.length);
            if (strcmp(value, group) == 0) {
                found_user = 1;
            }
            talloc_free(value);
            i++;
        }
    }
    else {
        fail("No memberOf attributes for user %s", username);
    }

    fail_unless(found_group, "User %s not a memberOf group %s", username, SYSDB_POSIX_TEST_GROUP);

    talloc_free(test_ctx);
}
END_TEST

Suite *create_sysdb_suite(void)
{
    Suite *s = suite_create("sysdb");

/* POSIX User test case */
    TCase *tc_posix_users = tcase_create("\tPOSIX Users");

    /* Create a new user */
    tcase_add_loop_test(tc_posix_users, test_sysdb_store_local_account_posix,26000,26010);

/* POSIX Group test case */
    TCase *tc_posix_gr = tcase_create("\tPOSIX Groups");

    /* Create a new group */
    tcase_add_loop_test(tc_posix_gr, test_sysdb_store_local_group_posix,27000,27001);

    /* Verify that the new group exists */
    tcase_add_loop_test(tc_posix_gr, test_sysdb_get_local_group_posix,27000,27001);

    /* Change the gid of the group we created */
    tcase_add_loop_test(tc_posix_gr, test_sysdb_store_local_group_posix,27001,27002);

    /* Verify that the group has been changed */
    tcase_add_loop_test(tc_posix_gr, test_sysdb_get_local_group_posix,27001,27002);

    /* Add users to the group */
    tcase_add_loop_test(tc_posix_gr, test_sysdb_add_acct_to_posix_group, 26000, 26010);

    /* Verify member and memberOf */
    tcase_add_loop_test(tc_posix_gr, test_sysdb_verify_posix_group_members, 26000, 26010);

/* Add all test cases to the test suite */
    suite_add_tcase(s, tc_posix_users);
    suite_add_tcase(s, tc_posix_gr);

    return s;
}

int main(int argc, const char *argv[]) {
    int opt;
    poptContext pc;
    int failure_count;
    Suite *sysdb_suite;
    SRunner *sr;

    struct poptOption long_options[] = {
        POPT_AUTOHELP
        SSSD_MAIN_OPTS
        { NULL }
    };

    pc = poptGetContext(argv[0], argc, argv, long_options, 0);
    while((opt = poptGetNextOpt(pc)) != -1) {
        switch(opt) {
        default:
            fprintf(stderr, "\nInvalid option %s: %s\n\n",
                    poptBadOption(pc, 0), poptStrerror(opt));
            poptPrintUsage(pc, stderr, 0);
            return 1;
        }
    }
    poptFreeContext(pc);

    sysdb_suite = create_sysdb_suite();
    sr = srunner_create(sysdb_suite);
    srunner_run_all(sr, CK_VERBOSE);
    failure_count = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failure_count==0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
