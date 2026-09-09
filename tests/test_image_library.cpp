#include <gtest/gtest.h>

#include "image_library.h"

TEST(ImageLibrary, AcceptsOnlyCanonicalImagePaths) {
    EXPECT_TRUE(ImageLibraryCatalog::is_directory_path("/images"));
    EXPECT_TRUE(ImageLibraryCatalog::is_directory_path("/images/holiday"));
    EXPECT_TRUE(ImageLibraryCatalog::is_image_path("/images/cover.PNG"));
    EXPECT_TRUE(ImageLibraryCatalog::is_image_path("/images/holiday/photo.jpeg"));
    EXPECT_FALSE(ImageLibraryCatalog::is_image_path("/images/holiday/2026/photo.jpg"));
    EXPECT_FALSE(ImageLibraryCatalog::is_image_path("/images//photo.jpg"));
    EXPECT_FALSE(ImageLibraryCatalog::is_image_path("/images/../secret.jpg"));
    EXPECT_FALSE(ImageLibraryCatalog::is_directory_path("/images/../"));
    EXPECT_FALSE(ImageLibraryCatalog::is_image_path("/images/holiday\\photo.jpg"));
    EXPECT_FALSE(ImageLibraryCatalog::is_image_path("/images/photo.gif"));
}

TEST(ImageLibrary, NormalizesStorageEntryNames) {
    char path[IMAGE_LIBRARY_PATH_MAX_LEN] = {};
    EXPECT_TRUE(image_library_child_path("/images", "photo.jpg", path, sizeof(path)));
    EXPECT_STREQ("/images/photo.jpg", path);
    EXPECT_TRUE(image_library_child_path("/images", "/images/photo.jpg", path, sizeof(path)));
    EXPECT_STREQ("/images/photo.jpg", path);
}

TEST(ImageLibrary, KeepsBoundedLexicographicCatalog) {
    ImageLibrarySnapshot snapshot = {};
    ImageLibraryCatalog catalog;
    catalog.begin(&snapshot);
    EXPECT_EQ(catalog.add("/images/zebra.jpg"), IMAGE_LIBRARY_OK);
    EXPECT_EQ(catalog.add("/images/alpha.png"), IMAGE_LIBRARY_OK);
    EXPECT_EQ(catalog.add("/images/mid.jpeg"), IMAGE_LIBRARY_OK);
    EXPECT_EQ(catalog.publish(), IMAGE_LIBRARY_OK);
    ASSERT_EQ(snapshot.count, 3);
    EXPECT_STREQ(snapshot.paths[0], "/images/alpha.png");
    EXPECT_STREQ(snapshot.paths[1], "/images/mid.jpeg");
    EXPECT_STREQ(snapshot.paths[2], "/images/zebra.jpg");
}

TEST(ImageLibrary, CursorWrapsAndRejectsEmptyAlbums) {
    ImageLibrarySnapshot snapshot = {};
    snapshot.available = true;
    snapshot.count = 2;
    strlcpy(snapshot.paths[0], "/images/a.jpg", sizeof(snapshot.paths[0]));
    strlcpy(snapshot.paths[1], "/images/b.jpg", sizeof(snapshot.paths[1]));
    ImageLibraryCursor cursor;
    ASSERT_TRUE(cursor.configure("/images"));
    EXPECT_STREQ(cursor.current(snapshot), "/images/a.jpg");
    EXPECT_STREQ(cursor.previous(snapshot), "/images/b.jpg");
    EXPECT_STREQ(cursor.next(snapshot), "/images/a.jpg");
    snapshot.count = 0;
    EXPECT_EQ(cursor.current(snapshot), nullptr);
    EXPECT_EQ(cursor.next(snapshot), nullptr);
}