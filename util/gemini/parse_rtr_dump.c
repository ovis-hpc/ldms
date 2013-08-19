#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * This is a standalone utility. It takes the output of rtr --interconnect
 * and creates processed output that can be read in by gem_link_util.c's
 * gem_link_perf_parse_interconnect function.
 */

int main(int argc, char* argv[])
{
	FILE *fd;
	char lbuf[256];
	char name[64];
	char* fname;
	char dir[8];
	char row[8];
	char column[8];
	char cabrow[8];
	char cabcol[8];
	char chassis[8];
	char slot[8];
	char gem[8];
	char type[32];
	char junk[32];
	int conn = 0;

	/*
	 * i = tile row, j = tile column, a = cabrow,
	 * b = cabcol, c = chassis, d = gem
	 */
	int i, j, a, b, c, d, e, f, k, count;
	int num_i = 6;
	int num_j = 8;
	int num_a = 2;
	int num_b = 2;
	int num_c = 2;
	int num_d = 2;
	int num_e = 3;
	int num_f = 8;
	int num_total = num_j * num_i * num_b * num_a *
			num_c * num_d * num_e * num_f;

	int rc;
	/* arrays of form
	 * array[e/o c#-][e/o -#][e/o c#][s#][e/0 g#][row][column]
	 * where e/o will be 0 if even and 1 if odd */
	int tileXP_array[2][2][2][2][3][8][6][8];
	int tileXM_array[2][2][2][2][3][8][6][8];
	int tileYP_array[2][2][2][2][3][8][6][8];
	int tileYM_array[2][2][2][2][3][8][6][8];
	int tileZP_array[2][2][2][2][3][8][6][8];
	int tileZM_array[2][2][2][2][3][8][6][8];
	int XP = 0, XM = 0, YP = 0, YM = 0, ZP = 0, ZM = 0;
	int tile_count = 0;

	char *s, *n;

	if (argc != 2){
	  printf("Usage: parse_rtr_dump <interconnect.txt> \n");
	  printf("\tinterconnect.txt -- file output of from --interconnect\n");
	  exit(-1);
	}

	fname = strdup(argv[1]);

	for (count = 0; count < num_total; count++) {
		k = count;
		/* Get values of all indices */
		j = k % num_j;
		k /= num_j;
		i = k % num_i;
		k /= num_i;
		f = k % num_f;
		k /= num_f;
		e = k % num_e;
		k /= num_e;
		d = k % num_d;
		k /= num_d;
		c = k % num_c;
		k /= num_c;
		b = k % num_b;
		k /= num_b;
		a = k % num_a;

		tileXP_array[a][b][c][d][e][f][i][j] = 0;
		tileXM_array[a][b][c][d][e][f][i][j] = 0;
		tileYP_array[a][b][c][d][e][f][i][j] = 0;
		tileYM_array[a][b][c][d][e][f][i][j] = 0;
		tileZP_array[a][b][c][d][e][f][i][j] = 0;
		tileZM_array[a][b][c][d][e][f][i][j] = 0;
	}

	fd = fopen(fname, "r");
	if (!fd) {
	  printf("Could not open the interconnect file <%s>\n", fname);
		free(fname);
		exit(-1);
	}
	fseek(fd, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), fd);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %s %s %s %s %s\n",
			    name, dir, junk, junk, junk, type);
		/* printf("name = %s, dir = %s\n", name, dir); */
		/* printf("name = %s, dir = %s, type = %s\n",
		 * name, dir, type);
		 */
		if (!strncmp(type, "mezza", 5))
			conn = 0;
		if (!strncmp(type, "backp", 5))
			conn = 1;
		if (!strncmp(type, "cable", 5))
			conn = 2;

		n = name;
		cabrow[0] = n[1];
		if (n[2] != '-') {
			cabrow[1] = n[2];
			cabrow[2] = '\0';
		}
		else cabrow[1] = '\0';

		while (*n != '-')
			*n++;

		cabcol[0] = n[1];
		if (n[2] != 'c') {
			cabcol[1] = n[2];
			cabcol[2] = '\0';
		}
		else cabcol[1] = '\0';

		while (*n != 'c')
			*n++;

		chassis[0] = n[1];
		chassis[1] = '\0';

		while (*n != 's')
			*n++;
		slot[0] = n[1];
		slot[1] = '\0';

		while (*n != 'g')
			*n++;
		gem[0] = n[1];
		gem[1] = '\0';

		while (*n != 'l')
			*n++;

		row[0] = n[1];
		row[1] = '\0';
		column[0] = n[2];
		column[1] = '\0';
		n[0] = '\0';
		i = atoi(row);
		j = atoi(column);
		a = atoi(cabrow);
		b = atoi(cabcol);
		c = atoi(chassis);
		d = atoi(gem);
		f = atoi(slot);

		/* printf("name = %s, cabrow = %d, cabcol = %d chassis = %d,
		 * gem = %d, row = %d, column = %d, dir = <%s>\n",
		 * name, a, b, c, d, i, j, dir);
		 */

		if (a % 2 == 0)
			a = 0;
		else
			a = 1;

		if (b % 2 == 0)
			b = 0;
		else
			b = 1;

		if (c % 2 == 0)
			c = 0;
		else
			c = 1;

		if (!strcmp(dir, "X+"))
			tileXP_array[a][b][c][d][conn][f][i][j] += 1;
		else if (!strcmp(dir, "X-"))
			tileXM_array[a][b][c][d][conn][f][i][j] += 1;
		else if (!strcmp(dir, "Y+"))
			tileYP_array[a][b][c][d][conn][f][i][j] += 1;
		else if (!strcmp(dir, "Y-"))
			tileYM_array[a][b][c][d][conn][f][i][j] += 1;
		else if (!strcmp(dir, "Z+"))
			tileZP_array[a][b][c][d][conn][f][i][j] += 1;
		else if (!strcmp(dir, "Z-"))
			tileZM_array[a][b][c][d][conn][f][i][j] += 1;
		else
			printf("Name %s with dir %s had no match\n", name, dir);
	} while (s);
	fclose (fd);

	for (count = 0; count < num_total; count++) {
		k = count;
		/* Get values of all indices */
		j = k % num_j;
		k /= num_j;
		i = k % num_i;
		k /= num_i;
		f = k % num_f;
		k /= num_f;
		e = k % num_e;
		k /= num_e;
		d = k % num_d;
		k /= num_d;
		c = k % num_c;
		k /= num_c;
		b = k % num_b;
		k /= num_b;
		a = k % num_a;

		if (tileXP_array[a][b][c][d][e][f][i][j] != 0) {
			/*
			 * printf("%d instances of X+ for tile %d,
			 * %d on cabrow %d, cabcol %d, chassis %d,
			 * gem %d\n", tileXP_array[a][b][c][d][i][j],
			 * i, j, a, b, c, d);
			 */
			if (f == 0){
				printf("%d%d%d%d%d%d X+ %d\n", a, b, c, d, i, j, e);
			}
			/* XP = XP + tileXP_array[i][j]; */
			/* tile_count += 1; */
		}
		if (tileXM_array[a][b][c][d][e][f][i][j] != 0) {
			/*
			 * printf("%d instances of X- for tile %d,
			 * %d on cabrow %d, cabcol %d, chassis %d,
			 * gem %d\n", tileXM_array[a][b][c][d][i][j],
			 * i, j, a, b, c, d);
			 */
			if (f == 0){
				printf("%d%d%d%d%d%d X- %d\n", a, b, c, d, i, j, e);
			}
			/* XM = XM + tileXM_array[i][j]; */
			/* tile_count += 1; */
		}
		if (tileYP_array[a][b][c][d][e][f][i][j] != 0) {
			/*
			 * printf("%d instances of Y+ for tile %d,
			 * %d on cabrow %d, cabcol %d, chassis %d,
			 * gem %d\n", tileYP_array[a][b][c][d][i][j],
			 * i, j, a, b, c, d);
			 */
			/*  */
			if (f == 0){
				printf("%d%d%d%d%d%d Y+ %d\n", a, b, c, d, i, j, e);
			}
			/* YP = YP + tileYP_array[i][j]; */
			/* tile_count += 1; */
		}
		if (tileYM_array[a][b][c][d][e][f][i][j] != 0) {
			/*
			 * printf("%d instances of Y- for tile %d,
			 * %d on cabrow %d, cabcol %d, chassis %d,
			 * gem %d\n", tileYM_array[a][b][c][d][i][j],
			 * i, j, a, b, c, d);
			 */
			if (f == 0){
				printf("%d%d%d%d%d%d Y- %d\n", a, b, c, d, i, j, e);
			}
			/* YM = YM + tileYM_array[i][j]; */
			/* tile_count += 1; */
		}
		if (tileZP_array[a][b][c][d][e][f][i][j] != 0) {
			/*
			 * printf("%d instances of Z+ for tile %d,
			 * %d on cabrow %d, cabcol %d, chassis %d,
			 * gem %d\n", tileZP_array[a][b][c][d][i][j],
			 * i, j, a, b, c, d);
			 */
			printf("%d%d%d%d%d%d Z+ %d %d\n", a, b, c, d, i, j, e, f);
			/* ZP = ZP + tileZP_array[i][j]; */
			/* tile_count += 1; */
		}
		if (tileZM_array[a][b][c][d][e][f][i][j] != 0) {
			/*
			 * printf("%d instances of Z- for tile %d,
			 * %d on cabrow %d, cabcol %d, chassis %d,
			 * gem %d\n", tileZM_array[a][b][c][d][i][j],
			 * i, j, a, b, c, d);
			 */
			printf("%d%d%d%d%d%d Z- %d %d\n", a, b, c, d, i, j, e, f);
			/* ZM = ZM + tileZM_array[i][j]; */
			/* tile_count += 1; */
		}
	}

	/*
	 * printf("X+ tiles = %d, X- tiles = %d, Y+ tiles = %d, Y- tiles = %d,
	 * Z+ tiles = %d, Z- tiles = %d,
	 * total links = %d\n", XP, XM, YP, YM, ZP, ZM, tile_count);
	 */

	free(fname);
}
