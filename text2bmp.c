#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "dot_matrix_font_to_bmp.h"
#include "encoding_convert.h"
#include "debug_log.h"

#define GB2312_HZK	"gb2312.hzk"

struct text_style {
	uint32_t left_margin;
	uint32_t right_margin;
	uint32_t up_margin;
	uint32_t down_margin;
	uint32_t line_spacing;
	uint32_t character_spacing;
	uint32_t max_line_length;
};

int main(int argc, char **argv)
{
	int opt;
	struct text_style style = {0};
	uint32_t bits_per_pix = 16;
	int color_anti_flag = 0;
	FILE *in = stdin;
	int gb2312_num;
	uint16_t *mem_addr;
	uint8_t linebuf[MAX_LINE];
	uint8_t gb2312buf[MAX_LINE * 2];
	uint8_t *ptr;
	uint8_t *ptr_gb2312;
	uint8_t unicode[2] = {0};
	uint16_t gb2312_code;
	uint8_t *addr_fd_in;
	struct stat fd_stat;
	int font_fd;
	int i;
	uint32_t offset;
	bmp_file_t bmp;
	bmp_file_t bmp_line;
	bmp_file_t bmp_all;
	uint32_t image_size;
	int ret;

	while ((opt = getopt(argc, argv, "l:r:u:d:i:c:m:b:o:")) != -1) {
		switch (opt) {
		case 'l': /* 左边距 */
			style.left_margin = strtol(optarg, NULL, 0);
			break;
		case 'r': /* 右边距 */
			style.right_margin = strtol(optarg, NULL, 0);
			break;
		case 'u': /* 上边距 */
			style.up_margin = strtol(optarg, NULL, 0);
			break;
		case 'd': /* 下边距 */
			style.down_margin = strtol(optarg, NULL, 0);
			break;
		case 'i': /* 行间距 */
			style.line_spacing = strtol(optarg, NULL, 0);
			break;
		case 'c': /* 字符间距 */
			style.character_spacing = strtol(optarg, NULL, 0);
			break;
		case 'm': /* 每行最大长度 */
			style.max_line_length = strtol(optarg, NULL, 0);
			break;
		case 'b': /* 位深 */
			bits_per_pix = strtol(optarg, NULL, 0);
			break;
		case 'o': /* 白底黑字 */
			color_anti_flag = 1;
			break;
		default:
			fprintf(stderr, "Usage: %s [-l left_margin] "
					"[-r right_margin] "
					"[-u up_margin] "
					"[-d down_margin] "
					"[-i line_spacing] "
					"[-c character_spacing] "
					"[-m max_line_length] "
					"[inputfile]\n", argv[0]);
			exit(1);
		}
	} /* while ((opt = getopt(argc, argv, "l:r:u:d:i:c:m:b:o:")) != -1) */
	if (argc > optind) {
		in = fopen(argv[optind], "r");
		if (!in) {
			fprintf(stderr,
				"Unable to open input file \"%s\": %s\n",
				argv[optind], strerror(errno));
			return 1;
		}
	}
	mem_addr = mem_gb2312("./GB2312", &gb2312_num);
	font_fd = open(GB2312_HZK, O_RDONLY);
	if (font_fd < 0) {
		perror("open");
		exit(1);
	}
	ret = fstat(font_fd, &fd_stat);
	if (ret == -1) {
		perror("fstat");
		exit(1);
	}
	addr_fd_in = mmap(NULL, (size_t) fd_stat.st_size,
			  PROT_READ, MAP_PRIVATE, font_fd, (off_t) 0);
	if (addr_fd_in == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	memset(&bmp_line, 0, sizeof(bmp_line));
	memset(&bmp_all, 0, sizeof(bmp_line));
	while (fgets((char *)linebuf, sizeof(linebuf) - 1, in)) {
		ptr = linebuf;
		ptr_gb2312 = gb2312buf;
		while (*ptr) {
			ret = utf8tounicode(ptr, unicode);
			if (ret < 0) {
				debug_print("utf8tounicode return %d\n", ret);
				exit(1);
			}
			ptr += ret;
			debug_print("u[0] = %#x u[1] = %#x", unicode[0], unicode[1]);
			gb2312_code = unicode_to_gb2312(unicode[0] + unicode[1] * 0x100, 
							mem_addr, 
							gb2312_num);
			ptr_gb2312[0] = gb2312_code % 0x100;
			if (gb2312_code / 0x100 > 0) {
				ptr_gb2312[1] = gb2312_code / 0x100;
				ptr_gb2312 += 2;
			} else
				ptr_gb2312 += 1;
		}
		ptr_gb2312[0] = '\0';
		fwrite(gb2312buf, 1, strlen((char *)gb2312buf), stdout);

		/*
		 * gb2312tobmps
		 */
		i = 0;
		for (;;) {
			if (gb2312buf[i] > 0xA0 && gb2312buf[i]  < 0xff) {
				offset = gb2312code_to_fontoffset(gb2312buf[i] + 0x100 * gb2312buf[i + 1]);
				i += 2;
			} else if (gb2312buf[i] > 0x1f && gb2312buf[i] < 0x80) {
				offset = gb2312code_to_fontoffset(0xa1a3 + 0x100 * (gb2312buf[i] - 0x21));
				i++;
			} else
				break;

			debug_print("offset = %#x", offset);
			memset(&bmp, 0, sizeof(bmp));
			set_header(&bmp, 16, 16, bits_per_pix);
			image_size = bmp.dib_h.image_size;
			bmp.pdata = malloc(image_size);
			memset(bmp.pdata, 0, image_size);
			fontdata2bmp(addr_fd_in + offset, 16, 16, &bmp, bits_per_pix, color_anti_flag);
			bmp_h_combin_2(&bmp_line, &bmp);
		} /* for (;;) */
		bmp_v_combin_3(&bmp_all, &bmp_line);
	} /* while (fgets((char *)linebuf, sizeof(linebuf) - 1, in)) */


	/*
	 * release
	 */
	free(bmp_all.pdata);
	free(bmp_line.pdata);
	ret = munmap(addr_fd_in, (size_t) fd_stat.st_size);
	if (ret == -1) {
		perror("munmap");
		exit(1);
	}
	close(font_fd);

	unmem_gb2312(mem_addr);
	if (in != stdin)
		fclose(in);
	return 0;
}
