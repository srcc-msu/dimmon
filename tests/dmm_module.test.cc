// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include "../dmm_module.c"

#include "CppUTest/CommandLineTestRunner.h"

// Stub for dmm_log, do nothing
void dmm_log(int pri, const char *format, ...)
{
    (void)pri;
    (void)format;
}

TEST_GROUP(TypeRegisterFind)
{
    void setup()
    {
        // Re-initialize type list before each test
        // May cause memory leaks, don't care for tests
        SLIST_INIT(&typelist);
    }
};

TEST(TypeRegisterFind, WillNotRegisterTypeWithEmptyName)
{
    static struct dmm_type TypeWithEmptyName = {
        "",
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        {},
    };
    CHECK_FALSE(dmm_type_register(&TypeWithEmptyName) == 0);
    CHECK(SLIST_EMPTY(&typelist));
};

TEST(TypeRegisterFind, WillNotRegisterTypeWithLongName)
{
    static struct dmm_type TypeWithLongName = {
        // Name length is 31 charachers
        "0123456789012345678901234567890",
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        {},
    };
    // Consciously make name longer than the array. It's for this test only
    strcat(TypeWithLongName.tp_name, "1");
    CHECK_FALSE(dmm_type_register(&TypeWithLongName) == 0);
    CHECK(SLIST_EMPTY(&typelist));
};

TEST(TypeRegisterFind, RegisterSingleType)
{
#define TESTTYPENAME "type_name"
    static struct dmm_type Type = {
        TESTTYPENAME,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        {},
    };
    // No type is found before registering
    POINTERS_EQUAL(NULL, dmm_type_find(TESTTYPENAME));
    CHECK(dmm_type_register(&Type) == 0);
    POINTERS_EQUAL(&Type, dmm_type_find(TESTTYPENAME));
#undef TESTTYPENAME
};

TEST(TypeRegisterFind, WillNotRegisterSameTypeTwice)
{
#define TESTTYPENAME "type_name"
    static struct dmm_type Type1 = {
        TESTTYPENAME,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        {},
    };
    static struct dmm_type Type2 = {
        TESTTYPENAME,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        {},
    };
    // No type is found before registering
    POINTERS_EQUAL(NULL, dmm_type_find(TESTTYPENAME));
    CHECK(dmm_type_register(&Type1) == 0);
    POINTERS_EQUAL(&Type1, dmm_type_find(TESTTYPENAME));
    // Check that the same type will not be registered
    CHECK_FALSE(dmm_type_register(&Type1) == 0);
    // Check that another type with the same name will not be registered
    CHECK_FALSE(dmm_type_register(&Type2) == 0);
    // Check that the first type is still registered
    POINTERS_EQUAL(&Type1, dmm_type_find(TESTTYPENAME));
#undef TESTTYPENAME
};

TEST(TypeRegisterFind, RegisterTwoTypes)
{
#define TESTTYPENAME1 "type_name1"
#define TESTTYPENAME2 "type_name2"
    static struct dmm_type Type1 = {
        TESTTYPENAME1,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        {},
    };
    static struct dmm_type Type2 = {
        TESTTYPENAME2,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        {},
    };
    // No type is found before registering
    POINTERS_EQUAL(NULL, dmm_type_find(TESTTYPENAME1));
    POINTERS_EQUAL(NULL, dmm_type_find(TESTTYPENAME2));
    CHECK(dmm_type_register(&Type1) == 0);
    CHECK(dmm_type_register(&Type2) == 0);
    POINTERS_EQUAL(&Type1, dmm_type_find(TESTTYPENAME1));
    POINTERS_EQUAL(&Type2, dmm_type_find(TESTTYPENAME2));
#undef TESTTYPENAME1
#undef TESTTYPENAME2
};

TEST_GROUP(ModuleLoad)
{
    void setup()
    {
        // Re-initialize type list before each test
        // May cause memory leaks, don't care for tests
        SLIST_INIT(&typelist);
    }
};

TEST(ModuleLoad, WillNotLoadNonexistentModule)
{
    CHECK_FALSE(dmm_module_load("/non/existent/dir/libnonexistent.so") == 0);
    CHECK(SLIST_EMPTY(&typelist));
};

TEST(ModuleLoad, WillNotLoadEmptyModule)
{
    CHECK_FALSE(dmm_module_load("./dmm_module.files/libempty_module.so") == 0);
    CHECK(SLIST_EMPTY(&typelist));
};

TEST(ModuleLoad, WillNotLoadModuleWrongABIVersion)
{
    CHECK_FALSE(dmm_module_load("./dmm_module.files/libmodule_wrong_abi.so") == 0);
    CHECK(SLIST_EMPTY(&typelist));
};

TEST(ModuleLoad, LoadModuleOneType)
{
    CHECK(dmm_module_load("./dmm_module.files/libmodule_one_type.so") == 0);
    dmm_type_p p = dmm_type_find("type_one");
    CHECK(p != NULL);
    STRCMP_EQUAL("type_one", p->tp_name);
};

TEST(ModuleLoad, LoadModuleTwoTypes)
{
    CHECK(dmm_module_load("./dmm_module.files/libmodule_two_types.so") == 0);
    dmm_type_p p1 = dmm_type_find("type_one");
    dmm_type_p p2 = dmm_type_find("type_two");
    CHECK(p1 != NULL);
    CHECK(p2 != NULL);
    STRCMP_EQUAL("type_one", p1->tp_name);
    STRCMP_EQUAL("type_two", p2->tp_name);
};

int main(int argc, char** argv)
{
    return CommandLineTestRunner::RunAllTests(argc, argv);
}

