/*
   +----------------------------------------------------------------------+
   | PHP Version 5 / zbarcode                                             |
   +----------------------------------------------------------------------+
   | Copyright (c) 2009 Mikko Koppanen                                    |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Mikko Kopppanen <mkoppanen@php.net>                          |
   +----------------------------------------------------------------------+
*/

#include "php_zbarcode.h"
#include "ext/standard/info.h"

/* Handlers */
static zend_object_handlers php_zbarcode_object_handlers;
static zend_object_handlers php_zbarcode_scanner_object_handlers;
static zend_object_handlers php_zbarcode_image_object_handlers;

/* Class entries */
zend_class_entry *php_zbarcode_sc_entry;
zend_class_entry *php_zbarcode_image_sc_entry;
zend_class_entry *php_zbarcode_scanner_sc_entry;
zend_class_entry *php_zbarcode_exception_class_entry;

#define Z_OBJ_P(zval_p) zend_objects_get_address(zval_p TSRMLS_CC)

/* Imagick support */
#ifdef HAVE_ZBARCODE_IMAGICK
#  include "ext/imagick/php_imagick_shared.h"
#endif

/* GD support */
#if HAVE_ZBARCODE_GD
# include "ext/gd/php_gd.h"
# if HAVE_GD_BUNDLED
#  include "ext/gd/libgd/gd.h"
# else 
#  include "gd.h"
# endif
#endif

#define PHP_ZBARCODE_RESOLUTION	1
#define PHP_ZBARCODE_ENHANCE	2
#define PHP_ZBARCODE_SHARPEN	4

#ifdef ZEND_ENGINE_3
	#define Z_ZBARCODE(obj) (php_zbarcode_object *)((char*)(obj) - XtOffsetOf(php_zbarcode_object, zo))
	#define Z_ZBARCODE_P(zv) Z_ZBARCODE(Z_OBJ_P(zv))
#else 
        #define Z_ZBARCODE(obj) (php_zbarcode_object *)((char*)(obj) - XtOffsetOf(php_zbarcode_object, zo))
	#define Z_ZBARCODE_P(zv) Z_ZBARCODE(Z_OBJ_P(zv))
#endif

#ifdef ZEND_ENGINE_3
	#define Z_ZBARCODE_IMAGE(obj) (php_zbarcode_image_object *)((char*)(obj) - XtOffsetOf(php_zbarcode_image_object, zo))
	#define Z_ZBARCODE_IMAGE_P(zv) Z_ZBARCODE_IMAGE(Z_OBJ_P(zv))
#else
	#define Z_ZBARCODE_IMAGE(obj) (php_zbarcode_image_object *)((char*)(obj) - XtOffsetOf(php_zbarcode_image_object, zo))
	#define Z_ZBARCODE_IMAGE_P(zv) Z_ZBARCODE_IMAGE(Z_OBJ_P(zv))
#endif

#ifdef ZEND_ENGINE_3
	#define Z_ZBARCODE_SCANNER(obj) (php_zbarcode_scanner_object *)((char*)(obj) - XtOffsetOf(php_zbarcode_scanner_object, zo))
	#define Z_ZBARCODE_SCANNER_P(zv) Z_ZBARCODE_SCANNER(Z_OBJ_P(zv))
#else
	#define Z_ZBARCODE_SCANNER_P(zv) (php_zbarcode_scanner_object *)zend_object_store_get_object(zv TSRMLS_CC)
#endif

#ifdef HAVE_ZBARCODE_IMAGICK
	#ifdef ZEND_ENGINE_3
		/* Structure for Imagick object. */
		typedef struct _php_imagick_object  {
			MagickWand *magick_wand;
			char *progress_monitor_name;
			zend_bool next_out_of_bound;
			zend_object zo;
		} php_imagick_object;

		#define Z_IMAGICK_P(zv) (php_imagick_object *)((char*)(Z_OBJ_P(zv)) - XtOffsetOf(php_imagick_object, zo))
	#else
		/* Structure for Imagick object. */
		typedef struct _php_imagick_object  {
			MagickWand *magick_wand;
			char *progress_monitor_name;
			zend_bool next_out_of_bound;
			zend_object zo;
		} php_imagick_object;

		#define Z_IMAGICK_P(zv) (php_imagick_object *)((char*)(Z_OBJ_P(zv)) - XtOffsetOf(php_imagick_object, zo))
	#endif
#endif

#ifdef ZEND_ENGINE_3
	#define ZBARCODE_LEN_TYPE size_t
#else
	#define ZBARCODE_LEN_TYPE int
#endif

#ifdef ZEND_ENGINE_3
	#define ZBARCODE_LONG_TYPE zend_long
#else
	#define ZBARCODE_LONG_TYPE long
#endif

static
void s_throw_image_exception (MagickWand *magick_wand, const char *message TSRMLS_DC)
{
	ExceptionType severity;
	char *magick_msg;

	magick_msg = MagickGetException (magick_wand, &severity);
	MagickClearException (magick_wand);

	if (magick_msg && strlen (magick_msg) > 0) {
		zend_throw_exception(php_zbarcode_exception_class_entry, magick_msg, 1 TSRMLS_CC);
		magick_msg = MagickRelinquishMemory(magick_msg);
		return;
	}

	if (magick_msg) {
		MagickRelinquishMemory(magick_msg);
	}

	zend_throw_exception(php_zbarcode_exception_class_entry, message, 1 TSRMLS_CC);
}

#define PHP_ZBARCODE_CHAIN_METHOD RETURN_ZVAL(getThis(), 1, 0);

static
zend_bool s_php_zbarcode_read(MagickWand *wand, char *filename, long enhance)
{
	if (enhance & PHP_ZBARCODE_RESOLUTION) {
		MagickSetResolution(wand, 200, 200);
	}

	if (MagickReadImage(wand, filename) == MagickFalse) {
		ClearMagickWand(wand);
		return 0;
	}

	if (enhance & PHP_ZBARCODE_ENHANCE) {
		MagickEnhanceImage(wand);
	}

	if (enhance & PHP_ZBARCODE_SHARPEN) {
		MagickSharpenImage(wand, 0, 0.5);
	}

	return 1;
}

/* {{{ zBarcodeImage zBarcodeImage::__construct([string filename, int enhance])
	Construct a new zBarcodeImage object
*/
PHP_METHOD(zbarcodeimage, __construct)
{
	php_zbarcode_image_object *intern;
	char *filename = NULL;
	ZBARCODE_LEN_TYPE filename_len = 0;
	ZBARCODE_LONG_TYPE enhance = 0;
	char resolved_path[MAXPATHLEN];

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s!l", &filename, &filename_len, &enhance) == FAILURE) {
		return;
	}

	if (!filename_len) {
		return;
	}

	if (!tsrm_realpath(filename, resolved_path TSRMLS_CC)) {
		zend_throw_exception(php_zbarcode_exception_class_entry, "The file does not exist or cannot be read", 1 TSRMLS_CC);
		return;
	}

	if (php_check_open_basedir(resolved_path TSRMLS_CC)) {
		return;
	}

	intern = Z_ZBARCODE_IMAGE_P(getThis());

	if (!s_php_zbarcode_read(intern->magick_wand, resolved_path, enhance)) {
		s_throw_image_exception (intern->magick_wand, "Unable to read the image" TSRMLS_CC);
		return;
	}
	return;
}
/* }}} */

/* {{{ zBarcodeImage zBarcodeImage::read(string filename[, int enhance])
	Read an image
*/
PHP_METHOD(zbarcodeimage, read)
{
	php_zbarcode_image_object *intern;
	char *filename;
	ZBARCODE_LEN_TYPE filename_len;
	ZBARCODE_LONG_TYPE enhance = 0;
	char resolved_path[MAXPATHLEN];

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l", &filename, &filename_len, &enhance) == FAILURE) {
		return;
	}

	if (!tsrm_realpath(filename, resolved_path TSRMLS_CC)) {
		zend_throw_exception(php_zbarcode_exception_class_entry, "The file does not exist or cannot be read", 1 TSRMLS_CC);
		return;
	}

	if (php_check_open_basedir(resolved_path TSRMLS_CC)) {
		return;
	}

	intern = Z_ZBARCODE_IMAGE_P(getThis());

	if (!s_php_zbarcode_read(intern->magick_wand, resolved_path, enhance)) {
		s_throw_image_exception (intern->magick_wand, "Unable to read the image" TSRMLS_CC);
		return;
	}
	PHP_ZBARCODE_CHAIN_METHOD;
}
/* }}} */

/* {{{ zBarcodeImage zBarcodeImage:clear()
	Remove existing pages
*/
PHP_METHOD(zbarcodeimage, clear)
{
	php_zbarcode_image_object *intern;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
		return;
	}

	intern = Z_ZBARCODE_IMAGE_P(getThis());
	ClearMagickWand(intern->magick_wand);

	PHP_ZBARCODE_CHAIN_METHOD;
}
/* }}} */

/* {{{ integer zBarcodeImage:count()
	Count pages
*/
PHP_METHOD(zbarcodeimage, count)
{
	php_zbarcode_image_object *intern;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
		return;
	}

	intern = Z_ZBARCODE_IMAGE_P(getThis());
	RETURN_LONG(MagickGetNumberImages(intern->magick_wand));
}
/* }}} */

static
zbar_image_t *s_php_zbarcode_image_create(unsigned long width, unsigned long height, unsigned char *image_data)
{
	zbar_image_t *image = zbar_image_create();

	if (!image) {
		return NULL;
	}

	zbar_image_set_format(image, *(int*)"Y800");
	zbar_image_set_size(image, width, height);
	zbar_image_set_data(image, (void *)image_data, width * height, zbar_image_free_data);
	return image;
}

static
zbar_image_t *s_php_zbarcode_get_page(MagickWand *wand)
{
	unsigned long width, height;
	unsigned char *image_data;
	size_t image_size;

	if (MagickSetImageDepth(wand, 8) == MagickFalse) {
		return NULL;
	}

	if (MagickSetImageFormat(wand, "GRAY") == MagickFalse) {
		return NULL;
	}

	width  = MagickGetImageWidth(wand);
	height = MagickGetImageHeight(wand);

	image_data = calloc(width * height, sizeof (char));

	if (!MagickExportImagePixels(wand, 0, 0, width, height, "I", CharPixel, image_data)) {
		return NULL;
	}
	return s_php_zbarcode_image_create(width, height, image_data);
}

static
zval *s_php_zbarcode_scan_page(zbar_image_scanner_t *scanner, zbar_image_t *image, zend_bool extended, zval *return_array TSRMLS_DC)
{
	int n;
	const zbar_symbol_t *symbol;

	array_init(return_array);

	/* scan the image for barcodes */
	n = zbar_scan_image(scanner, image);

	/* extract results */
	symbol = zbar_image_first_symbol(image);

	/* Loop through all all symbols */
	for(; symbol; symbol = zbar_symbol_next(symbol)) {
		zbar_symbol_type_t symbol_type;
		const char *data;
		unsigned int loc_size;

		zval _symbol_array, *symbol_array = &_symbol_array;
#ifndef ZEND_ENGINE_3
		MAKE_STD_ZVAL(symbol_array)
#endif

		/* Initialize array element */
		array_init(symbol_array);

		/* Get symbol type and data in it */
		symbol_type = zbar_symbol_get_type(symbol);
		data = zbar_symbol_get_data(symbol);

#ifdef ZEND_ENGINE_3
		add_assoc_string(symbol_array, "data", (char *)data);
		add_assoc_string(symbol_array, "type", (char *)zbar_get_symbol_name(symbol_type));
#else
		add_assoc_string(symbol_array, "data", (char *)data, 1);
		add_assoc_string(symbol_array, "type", (char *)zbar_get_symbol_name(symbol_type), 1);
#endif
#ifdef HAVE_ZBAR_SYMBOL_GET_QUALITY
		add_assoc_long(symbol_array, "quality", zbar_symbol_get_quality(symbol));
#endif		
		if (extended) {
			unsigned int i;

			zval _loc_array, *loc_array = &_loc_array;
#ifndef ZEND_ENGINE_3
			MAKE_STD_ZVAL(loc_array)
#endif

			array_init(loc_array);
			loc_size = zbar_symbol_get_loc_size(symbol);

			for (i = 0; i < loc_size; i++) {
				zval _coords, *coords = &_coords;
#ifndef ZEND_ENGINE_3
				MAKE_STD_ZVAL(coords)
#endif

				array_init(coords);

				add_assoc_long(coords, "x", zbar_symbol_get_loc_x(symbol, i));
				add_assoc_long(coords, "y", zbar_symbol_get_loc_y(symbol, i));

				add_next_index_zval(loc_array, coords);
			}
			add_assoc_zval(symbol_array, "location", loc_array);
		}
		add_next_index_zval(return_array, symbol_array);
	}
	return return_array;
}

/* {{{ array zBarCodeScanner::scan(mixed image[, int page_num, bool extended])
	Scan an image
*/
PHP_METHOD(zbarcodescanner, scan)
{
	zval *image;
	MagickWand *magick_wand;

	zbar_image_t *page;
	php_zbarcode_scanner_object *intern;

	zval *object;
	int i = 1;
	zend_bool extended = 0;
	ZBARCODE_LONG_TYPE page_num = 1;
	long image_count;
	zend_bool free_ptr = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|lb", &image, &page_num, &extended) == FAILURE) {
		return;
	}

#ifdef HAVE_ZBARCODE_GD
	if (Z_TYPE_P(image) != IS_OBJECT && Z_TYPE_P(image) != IS_RESOURCE) {
		zend_throw_exception(php_zbarcode_exception_class_entry, "The first parameter must be a valid object or a GD resource", 1 TSRMLS_CC);
		return;
	}
#else
	if (Z_TYPE_P(image) != IS_OBJECT) {
		zend_throw_exception(php_zbarcode_exception_class_entry, "The first parameter must be a valid object", 1 TSRMLS_CC);
		return;
	}
#endif

	intern = Z_ZBARCODE_SCANNER_P(getThis());

	if (Z_TYPE_P(image) == IS_OBJECT && instanceof_function_ex(Z_OBJCE_P(image), php_zbarcode_image_sc_entry, 0 TSRMLS_CC)) {
		php_zbarcode_image_object *intern_image;

		intern_image = Z_ZBARCODE_IMAGE_P(image);
		magick_wand  = intern_image->magick_wand;
	}
#ifdef HAVE_ZBARCODE_IMAGICK	
	else if (Z_TYPE_P(image) == IS_OBJECT && instanceof_function_ex(Z_OBJCE_P(image), php_imagick_get_class_entry(), 0 TSRMLS_CC)) {
		php_imagick_object *intern_image;
		intern_image = Z_IMAGICK_P(image);
		magick_wand  = intern_image->magick_wand;
	}
#endif	
#ifdef HAVE_ZBARCODE_GD
	/* Handle GD resource */
	else if (Z_TYPE_P(image) == IS_RESOURCE) {
		gdImagePtr gd_image = NULL;
		MagickWand *wand;
		unsigned long image_w, image_h;
		PixelWand *color;
		MagickBooleanType status;
		unsigned char *pixels;
		int x, y, pixel_pos = 0;
		zend_bool has_pixels = 0;

#ifdef ZEND_ENGINE_3
		gd_image = (gdImagePtr)zend_fetch_resource(Z_RES_P(image), "Image", phpi_get_le_gd());
#else
		ZEND_FETCH_RESOURCE(gd_image, gdImagePtr, &image, -1, "Image", phpi_get_le_gd());
#endif
		if (!gd_image) {
			zend_throw_exception(php_zbarcode_exception_class_entry, "The given resource is not a valid GD image", 1 TSRMLS_CC);
			return;
		}

		image_w = gdImageSX(gd_image);
		image_h = gdImageSY(gd_image);

		pixels = emalloc((3 * image_w) * image_h);

		for (y = 0; y < image_h; y++) {
			for (x = 0; x < image_w; x++) {
				int pixel = 0;
				if (gdImageTrueColor(gd_image)) {
					if (gd_image->tpixels && gdImageBoundsSafe(gd_image, x, y)) {
						pixel = gdImageTrueColorPixel(gd_image, x, y);
						pixels[pixel_pos++] = (pixel >> 16) & 0xFF;
						pixels[pixel_pos++] = (pixel >> 8) & 0xFF;
						pixels[pixel_pos++] = pixel & 0xFF;
						has_pixels = 1;
					}
				} else {
					if (gd_image->pixels && gdImageBoundsSafe(gd_image, x, y)) {
						pixel = gd_image->pixels[y][x];
						pixels[pixel_pos++] = gd_image->red[pixel];
						pixels[pixel_pos++] = gd_image->green[pixel];
					 	pixels[pixel_pos++] = gd_image->blue[pixel];
						has_pixels = 1;
					}
				}
			}
		}

		if (!has_pixels) {
			efree(pixels);
			zend_throw_exception(php_zbarcode_exception_class_entry, "Unable to get pixels from GD resource", 1 TSRMLS_CC);
			return;
		}

		wand = NewMagickWand();
		if (!wand) {
			efree(pixels);
			zend_throw_exception(php_zbarcode_exception_class_entry, "Failed to allocate MagickWand structure", 1 TSRMLS_CC);
			return;
		}

		color = NewPixelWand();
		if (!color) {
			efree(pixels);
			DestroyMagickWand(wand);
			zend_throw_exception(php_zbarcode_exception_class_entry, "Failed to allocate PixelWand structure", 1 TSRMLS_CC);
			return;
		}

		if (MagickNewImage(wand, image_w, image_h, color) == MagickFalse) {
			efree(pixels);
			DestroyMagickWand(wand);
			DestroyPixelWand(color);
			return;
		}
		DestroyPixelWand(color);

		status = MagickSetImagePixels(wand, 0, 0, image_w, image_h, "RGB", CharPixel, pixels);
		efree(pixels);

		if (status == MagickFalse) {
			DestroyMagickWand(wand);
			return;
		}

		magick_wand = wand;
		page_num = 0;
		free_ptr = 1;
	}
#endif
	else {
		zend_throw_exception(php_zbarcode_exception_class_entry, "Invalid parameter type", 1 TSRMLS_CC);
		return;
	}

	image_count = MagickGetNumberImages(magick_wand);

	if (image_count == 0) {
		zend_throw_exception(php_zbarcode_exception_class_entry, "The image object does not contain images", 1 TSRMLS_CC);
		return;
	}

	if (page_num > 0) {
		if (image_count < page_num) {
			if (free_ptr) {
				DestroyMagickWand(magick_wand);
			}
			zend_throw_exception_ex(php_zbarcode_exception_class_entry, 1 TSRMLS_CC, "Invalid page specified. The object contains %d page(s)", image_count);
			return;
		}

		if (MagickSetIteratorIndex(magick_wand, page_num - 1) == MagickFalse) {
			if (free_ptr) {
				DestroyMagickWand(magick_wand);
			}
			zend_throw_exception(php_zbarcode_exception_class_entry, "Failed to set the page number", 1 TSRMLS_CC);
			return;
		}
		/* Read page */
		page = s_php_zbarcode_get_page(magick_wand);

		if (!page) {
			if (free_ptr) {
				DestroyMagickWand(magick_wand);
			}
			zend_throw_exception(php_zbarcode_exception_class_entry, "Failed to get the page", 1 TSRMLS_CC);
			return;
		}

		/* Scan the page for barcodes */
		s_php_zbarcode_scan_page(intern->scanner, page, extended, return_value TSRMLS_CC);
	} else {
		array_init(return_value);

		MagickResetIterator(magick_wand);
		while (MagickNextImage(magick_wand) != MagickFalse) {
			zval _page_array, *page_array = &_page_array;

#ifndef ZEND_ENGINE_3
			MAKE_STD_ZVAL(page_array)
#endif

			/* Read the current page */
			page = s_php_zbarcode_get_page(magick_wand);

			/* Reading current page failed */
			if (!page) {
				i++;
				continue;
			}
			/* Scan the page for barcodes */
			s_php_zbarcode_scan_page(intern->scanner, page, extended, page_array TSRMLS_CC);
			add_index_zval(return_value, i++, page_array);
		}
	}
	
	zbar_image_destroy(page);

	if (free_ptr) {
		DestroyMagickWand(magick_wand);
	}

	return;
}
/* }}} */

/* {{{ zBarCodeScanner zBarCodeScanner::setConfig(int name, int value[, int symbology])
	Set config option
*/
PHP_METHOD(zbarcodescanner, setconfig)
{
	php_zbarcode_scanner_object *intern;
	long symbology = 0, name, value;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ll|l", &name, &value, &symbology) == FAILURE) {
		return;
	}
	intern = Z_ZBARCODE_SCANNER_P(getThis());

	if (zbar_image_scanner_set_config(intern->scanner, symbology, name, value) != 0) {
		zend_throw_exception(php_zbarcode_exception_class_entry, "Config does not apply to specified symbology, or value out of range", 1 TSRMLS_CC);
		return;
	}
	PHP_ZBARCODE_CHAIN_METHOD;
}
/* }}} */

static zend_function_entry php_zbarcode_class_methods[] =
{
	{ NULL, NULL, NULL }
};

ZEND_BEGIN_ARG_INFO_EX(zbarcode_image_construct_args, 0, 0, 0)
	ZEND_ARG_INFO(0, filename)
	ZEND_ARG_INFO(0, enhance)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(zbarcode_image_read_args, 0, 0, 1)
	ZEND_ARG_INFO(0, filename)
	ZEND_ARG_INFO(0, enhance)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(zbarcode_image_no_args, 0, 0, 0)
ZEND_END_ARG_INFO()

static zend_function_entry php_zbarcode_image_class_methods[] =
{
	PHP_ME(zbarcodeimage, __construct,	zbarcode_image_construct_args, 	ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(zbarcodeimage, read,			zbarcode_image_read_args, 		ZEND_ACC_PUBLIC)
	PHP_ME(zbarcodeimage, clear,		zbarcode_image_no_args, 		ZEND_ACC_PUBLIC)
	PHP_ME(zbarcodeimage, count,		zbarcode_image_no_args, 		ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

ZEND_BEGIN_ARG_INFO_EX(zbarcode_scanner_scan_args, 0, 0, 1)
	ZEND_ARG_INFO(0, image)
	ZEND_ARG_INFO(0, page_num)
	ZEND_ARG_INFO(0, extended)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(zbarcode_scanner_setconfig_args, 0, 0, 2)
	ZEND_ARG_INFO(0, name)
	ZEND_ARG_INFO(0, value)
	ZEND_ARG_INFO(0, symbology)
ZEND_END_ARG_INFO()

static zend_function_entry php_zbarcode_scanner_class_methods[] =
{
	PHP_ME(zbarcodescanner, scan,		zbarcode_scanner_scan_args, 		ZEND_ACC_PUBLIC)
	PHP_ME(zbarcodescanner, setconfig,	zbarcode_scanner_setconfig_args, 	ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

/* {{{ static void php_zbarcode_object_free_storage(void *object TSRMLS_DC)
*/
#ifdef ZEND_ENGINE_3
static void php_zbarcode_object_free_storage(zend_object *object)
#else
static void php_zbarcode_object_free_storage(void *object TSRMLS_DC)
#endif
{
#ifdef ZEND_ENGINE_3
	zend_object_std_dtor(object);
#else
	php_zbarcode_object *intern = (php_zbarcode_object *)object;
	zend_object_std_dtor(&intern->zo TSRMLS_CC);
#endif

#ifndef ZEND_ENGINE_3
	efree(intern);
#endif
}
/* }}} */

/* {{{ static void php_zbarcode_scanner_object_free_storage(void *object TSRMLS_DC)
*/
#ifdef ZEND_ENGINE_3
static void php_zbarcode_scanner_object_free_storage(zend_object *object)
#else
static void php_zbarcode_scanner_object_free_storage(void *object TSRMLS_DC)
#endif
{
	php_zbarcode_scanner_object *intern;

#ifdef ZEND_ENGINE_3
	intern = Z_ZBARCODE_SCANNER(object);
#else
	intern = (php_zbarcode_scanner_object *)object;
#endif

	if (intern->scanner) {
		zbar_image_scanner_destroy(intern->scanner);
		intern->scanner = NULL;
	}
	zend_object_std_dtor(&intern->zo TSRMLS_CC);

#ifndef ZEND_ENGINE_3
	efree(intern);
#endif
}
/* }}} */

/* {{{ static void php_zbarcode_image_object_free_storage(void *object TSRMLS_DC)
*/
#ifdef ZEND_ENGINE_3
static void php_zbarcode_image_object_free_storage(zend_object *object)
#else
static void php_zbarcode_image_object_free_storage(void *object TSRMLS_DC)
#endif
{
	php_zbarcode_image_object *intern;

#ifdef ZEND_ENGINE_3
	intern = Z_ZBARCODE_IMAGE(object);
#else
	intern = (php_zbarcode_image_object *)object;
#endif

	if (intern->magick_wand) {
		DestroyMagickWand(intern->magick_wand);
		intern->magick_wand = NULL;
	}
	zend_object_std_dtor(&intern->zo TSRMLS_CC);

#ifndef ZEND_ENGINE_3
	efree(intern);
#endif
}
/* }}} */

/* PHP 5.4 */
#if PHP_VERSION_ID < 50399
# define object_properties_init(zo, class_type) { \
			zval *tmp; \
			zend_hash_copy((*zo).properties, \
							&class_type->default_properties, \
							(copy_ctor_func_t) zval_add_ref, \
							(void *) &tmp, \
							sizeof(zval *)); \
		 }
#endif

/* {{{ static zend_object *php_zbarcode_object_new(zend_class_entry *class_type)
*/
#ifdef ZEND_ENGINE_3
static zend_object *php_zbarcode_object_new(zend_class_entry *class_type)
#else
static zend_object_value php_zbarcode_object_new(zend_class_entry *class_type TSRMLS_DC)
#endif
{
	zval *tmp;
	php_zbarcode_object *intern;

#ifdef ZEND_ENGINE_3
	intern = ecalloc(1,
			 sizeof(php_zbarcode_object) +
			 zend_object_properties_size(class_type));
#else
	/* Allocate memory for it */
	intern = emalloc(sizeof(php_zbarcode_object));
	memset(&intern->zo, 0, sizeof(php_zbarcode_object));
#endif

	zend_object_std_init(&intern->zo, class_type TSRMLS_CC);
	object_properties_init(&intern->zo, class_type);

#ifdef ZEND_ENGINE_3
	intern->zo.handlers = &php_zbarcode_object_handlers;
	return &intern->zo;
#else
	zend_object_value retval;
	retval.handle = zend_objects_store_put(intern, NULL, (zend_objects_free_object_storage_t) php_zbarcode_object_free_storage, NULL TSRMLS_CC);
	retval.handlers = (zend_object_handlers *) &php_zbarcode_object_handlers;
	return retval;
#endif

}
/* }}} */

/* {{{ static zend_object *php_zbarcode_scanner_object_new(zend_class_entry *class_type)
*/
#ifdef ZEND_ENGINE_3
static zend_object *php_zbarcode_scanner_object_new(zend_class_entry *class_type)
#else
static zend_object_value php_zbarcode_scanner_object_new(zend_class_entry *class_type TSRMLS_DC)
#endif
{
	zval *tmp;
	php_zbarcode_scanner_object *intern;

#ifdef ZEND_ENGINE_3
	intern = ecalloc(1,
			 sizeof(php_zbarcode_scanner_object) +
			 zend_object_properties_size(class_type));
#else
	/* Allocate memory for it */
	intern = emalloc(sizeof(php_zbarcode_scanner_object));
	memset(&intern->zo, 0, sizeof(php_zbarcode_scanner_object));
#endif

	/* Initialize reader */
	intern->scanner = zbar_image_scanner_create();

	zend_object_std_init(&intern->zo, class_type TSRMLS_CC);
	object_properties_init(&intern->zo, class_type);

#ifdef ZEND_ENGINE_3
	intern->zo.handlers = &php_zbarcode_scanner_object_handlers;
	return &intern->zo;
#else
	zend_object_value retval;

	retval.handle = zend_objects_store_put(intern, NULL, (zend_objects_free_object_storage_t) php_zbarcode_scanner_object_free_storage, NULL TSRMLS_CC);
	retval.handlers = (zend_object_handlers *) &php_zbarcode_scanner_object_handlers;
	return retval;
#endif
}
/* }}} */

/* {{{ static zend_object *php_zbarcode_image_object_new(zend_class_entry *class_type)
*/
#ifdef ZEND_ENGINE_3
static zend_object *php_zbarcode_image_object_new(zend_class_entry *class_type)
#else
static zend_object_value php_zbarcode_image_object_new(zend_class_entry *class_type TSRMLS_DC)
#endif
{
	zval *tmp;
	php_zbarcode_image_object *intern;

#ifdef ZEND_ENGINE_3
	intern = ecalloc(1,
			 sizeof(php_zbarcode_image_object) +
			 zend_object_properties_size(class_type));
#else
	/* Allocate memory for it */
	intern = emalloc(sizeof(php_zbarcode_image_object));
	memset(&intern->zo, 0, sizeof(php_zbarcode_image_object));
#endif

	/* Initialize reader */
	intern->magick_wand = NewMagickWand();

	zend_object_std_init(&intern->zo, class_type TSRMLS_CC);
	object_properties_init(&intern->zo, class_type);

#ifdef ZEND_ENGINE_3
	intern->zo.handlers = &php_zbarcode_image_object_handlers;
	return &intern->zo;
#else
	zend_object_value retval;

	retval.handle = zend_objects_store_put(intern, NULL, (zend_objects_free_object_storage_t) php_zbarcode_image_object_free_storage, NULL TSRMLS_CC);
	retval.handlers = (zend_object_handlers *) &php_zbarcode_image_object_handlers;
	return retval;
#endif
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION(zbarcode)
*/
PHP_MINIT_FUNCTION(zbarcode)
{
	zend_class_entry ce;
	memcpy(&php_zbarcode_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	memcpy(&php_zbarcode_scanner_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	memcpy(&php_zbarcode_image_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));

	MagickWandGenesis();

	/*
		Initialize exceptions (zbarcode exception)
	*/
	INIT_CLASS_ENTRY(ce, "zbarcodeexception", NULL);
#ifdef ZEND_ENGINE_3
	php_zbarcode_exception_class_entry = zend_register_internal_class_ex(&ce, zend_ce_exception);
#else
	php_zbarcode_exception_class_entry = zend_register_internal_class_ex(&ce, zend_exception_get_default(TSRMLS_C), NULL TSRMLS_CC);
#endif
	php_zbarcode_exception_class_entry->ce_flags |= ZEND_ACC_FINAL;

	/*
		Initialize the class (zbarcode). This class is just a container for constants
	*/
	INIT_CLASS_ENTRY(ce, "zbarcode", php_zbarcode_class_methods);
	php_zbarcode_object_handlers.clone_obj = NULL;
#ifdef ZEND_ENGINE_3
	php_zbarcode_object_handlers.free_obj = php_zbarcode_object_free_storage;
	php_zbarcode_object_handlers.offset = XtOffsetOf(php_zbarcode_object, zo);
#endif
	php_zbarcode_sc_entry = zend_register_internal_class(&ce TSRMLS_CC);
	php_zbarcode_sc_entry->create_object = php_zbarcode_object_new;

	/*
		Initialize the class (zbarcode image)
	*/
	INIT_CLASS_ENTRY(ce, "zbarcodeimage", php_zbarcode_image_class_methods);
	php_zbarcode_image_object_handlers.clone_obj = NULL;
#ifdef ZEND_ENGINE_3
	php_zbarcode_image_object_handlers.free_obj = php_zbarcode_image_object_free_storage;
	php_zbarcode_image_object_handlers.offset = XtOffsetOf(php_zbarcode_image_object, zo);
#endif
	php_zbarcode_image_sc_entry = zend_register_internal_class(&ce TSRMLS_CC);
	php_zbarcode_image_sc_entry->create_object = php_zbarcode_image_object_new;

	/*
		Initialize the class (zbarcode scanner)
	*/
	INIT_CLASS_ENTRY(ce, "zbarcodescanner", php_zbarcode_scanner_class_methods);
	php_zbarcode_scanner_object_handlers.clone_obj = NULL;
#ifdef ZEND_ENGINE_3
	php_zbarcode_scanner_object_handlers.free_obj = php_zbarcode_scanner_object_free_storage;
	php_zbarcode_scanner_object_handlers.offset = XtOffsetOf(php_zbarcode_scanner_object, zo);
#endif
	php_zbarcode_scanner_sc_entry = zend_register_internal_class(&ce TSRMLS_CC);
	php_zbarcode_scanner_sc_entry->create_object = php_zbarcode_scanner_object_new;

	/* Do we have imagick support */
#ifdef HAVE_ZBARCODE_IMAGICK
	zend_declare_class_constant_bool(php_zbarcode_sc_entry, "HAVE_IMAGICK", sizeof("HAVE_IMAGICK")-1, 1 TSRMLS_CC);
#else 
	zend_declare_class_constant_bool(php_zbarcode_sc_entry, "HAVE_IMAGICK", sizeof("HAVE_IMAGICK")-1, 0 TSRMLS_CC);
#endif

	/* Do we have GD support */
#ifdef HAVE_ZBARCODE_GD
	zend_declare_class_constant_bool(php_zbarcode_sc_entry, "HAVE_GD", sizeof("HAVE_GD")-1, 1 TSRMLS_CC);
#else 
	zend_declare_class_constant_bool(php_zbarcode_sc_entry, "HAVE_GD", sizeof("HAVE_GD")-1, 0 TSRMLS_CC);
#endif

#define PHP_ZBARCODE_REGISTER_CONST_LONG(const_name, value) \
	zend_declare_class_constant_long(php_zbarcode_sc_entry, const_name, sizeof(const_name)-1, (long)value TSRMLS_CC);

	PHP_ZBARCODE_REGISTER_CONST_LONG("CFG_ENABLE", ZBAR_CFG_ENABLE);			/**< enable symbology/feature */
	PHP_ZBARCODE_REGISTER_CONST_LONG("CFG_ADD_CHECK", ZBAR_CFG_ADD_CHECK);		/**< enable check digit when optional */
	PHP_ZBARCODE_REGISTER_CONST_LONG("CFG_EMIT_CHECK", ZBAR_CFG_EMIT_CHECK);	/**< return check digit when present */
	PHP_ZBARCODE_REGISTER_CONST_LONG("CFG_ASCII", ZBAR_CFG_ASCII);				/**< enable full ASCII character set */
	PHP_ZBARCODE_REGISTER_CONST_LONG("CFG_NUM", ZBAR_CFG_NUM);					/**< number of boolean decoder configs */
	PHP_ZBARCODE_REGISTER_CONST_LONG("CFG_MIN_LEN", ZBAR_CFG_MIN_LEN);			/**< minimum data length for valid decode */
	PHP_ZBARCODE_REGISTER_CONST_LONG("CFG_MAX_LEN", ZBAR_CFG_MAX_LEN);			/**< maximum data length for valid decode */
#ifdef HAVE_ZBAR_SYMBOL_GET_QUALITY
	PHP_ZBARCODE_REGISTER_CONST_LONG("CFG_POSITION", ZBAR_CFG_POSITION);		/**< enable scanner to collect position data */
#endif
	PHP_ZBARCODE_REGISTER_CONST_LONG("CFG_X_DENSITY", ZBAR_CFG_X_DENSITY);		/**< image scanner vertical scan density */
	PHP_ZBARCODE_REGISTER_CONST_LONG("CFG_Y_DENSITY", ZBAR_CFG_Y_DENSITY);		/**< image scanner horizontal scan density */

	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_ALL", 0);						/**< all symbologies */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_NONE", ZBAR_NONE);			/**< no symbol decoded */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_PARTIAL", ZBAR_PARTIAL);		/**< intermediate status */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_EAN8", ZBAR_EAN8);			/**< EAN-8 */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_UPCE", ZBAR_UPCE);			/**< UPC-E */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_ISBN10", ZBAR_ISBN10);		/**< ISBN-10 (from EAN-13).*/
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_UPCA", ZBAR_UPCA);			/**< UPC-A */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_EAN13", ZBAR_EAN13);			/**< EAN-13 */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_ISBN13", ZBAR_ISBN13);		/**< ISBN-13 (from EAN-13).  */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_I25", ZBAR_I25);				/**< Interleaved 2 of 5. */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_CODE39", ZBAR_CODE39);		/**< Code 39.  */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_PDF417", ZBAR_PDF417);		/**< PDF417. */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_CODE128", ZBAR_CODE128);		/**< Code 128 */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_SYMBOL", ZBAR_SYMBOL);		/**< mask for base symbol type */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_ADDON2", ZBAR_ADDON2);		/**< 2-digit add-on flag */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_ADDON5", ZBAR_ADDON5);		/**< 5-digit add-on flag */
	PHP_ZBARCODE_REGISTER_CONST_LONG("SYM_ADDON", ZBAR_ADDON);			/**< add-on flag mask */
	PHP_ZBARCODE_REGISTER_CONST_LONG("OPT_RESOLUTION", PHP_ZBARCODE_RESOLUTION);	/**< Set higher resolution */
	PHP_ZBARCODE_REGISTER_CONST_LONG("OPT_ENHANCE", PHP_ZBARCODE_ENHANCE);			/**< Reduce noise */
	PHP_ZBARCODE_REGISTER_CONST_LONG("OPT_SHARPEN", PHP_ZBARCODE_SHARPEN);			/**< Sharpen */

#undef PHP_ZBARCODE_REGISTER_CONST_LONG                                                        

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION(zbarcode)
*/
PHP_MSHUTDOWN_FUNCTION(zbarcode)
{
	MagickWandTerminus();
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION(zbarcode)
*/
PHP_MINFO_FUNCTION(zbarcode)
{
	unsigned int major = 0, minor = 0;
	char *zbar_ver = NULL;
	unsigned long magick_version;

	zbar_version(&major, &minor);
	spprintf(&zbar_ver, 24, "%d.%d.%d", major, minor);

	php_info_print_table_start();
	php_info_print_table_row(2, "zbarcode module",			"enabled");
	php_info_print_table_row(2, "zbarcode module version",	PHP_ZBARCODE_EXTVER);
	php_info_print_table_row(2, "ZBar library version",		zbar_ver);
	php_info_print_table_row(2, "ImageMagick version",		MagickGetVersion(&magick_version));

#ifdef HAVE_ZBARCODE_IMAGICK
	php_info_print_table_row(2, "Imagick support", "enabled");
#else 
	php_info_print_table_row(2, "Imagick support", "disabled");
#endif

#ifdef HAVE_ZBARCODE_GD
	php_info_print_table_row(2, "GD support", "enabled");
#else 
	php_info_print_table_row(2, "GD support", "disabled");
#endif

	php_info_print_table_end();
	efree(zbar_ver);
}
/* }}} */

static zend_function_entry php_zbarcode_functions[] =
{
	{ NULL, NULL, NULL }
};

zend_module_entry zbarcode_module_entry =
{
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	PHP_ZBARCODE_EXTNAME,
	php_zbarcode_functions,		/* Functions */
	PHP_MINIT(zbarcode),		/* MINIT */
	PHP_MSHUTDOWN(zbarcode),	/* MSHUTDOWN */
	NULL,						/* RINIT */
	NULL,						/* RSHUTDOWN */
	PHP_MINFO(zbarcode),		/* MINFO */
#if ZEND_MODULE_API_NO >= 20010901
	PHP_ZBARCODE_EXTVER,
#endif
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_ZBARCODE
ZEND_GET_MODULE(zbarcode)
#endif
