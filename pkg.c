/*
 * pkg.c
 * higher-level dependency graph compilation, management and manipulation
 *
 * Copyright (c) 2011, 2012 William Pitcock <nenolod@dereferenced.org>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "pkg.h"
#include "bsdstubs.h"

#define PKG_CONFIG_EXT		".pc"
#define PKG_CONFIG_PATH_SZ	(65535)

/* pkg-config uses ';' on win32 as ':' is part of path */
#ifdef _WIN32
#define PKG_CONFIG_PATH_SEP_S	";"
#else
#define PKG_CONFIG_PATH_SEP_S	":"
#endif

static inline size_t
path_split(const char *text, char ***parv)
{
	size_t count = 0;
	char *workbuf, *p, *iter;

	if (text == NULL)
		return 0;

	*parv = malloc(sizeof (void *));

	iter = workbuf = strdup(text);
	while ((p = strtok(iter, PKG_CONFIG_PATH_SEP_S)) != NULL)
	{
		(*parv)[count] = strdup(p);
		count++, iter = NULL;

		*parv = realloc(*parv, sizeof (void *) * count);
	}
	free(workbuf);

	return count + 1;
}

pkg_t *
pkg_find(const char *name, unsigned int flags)
{
	char locbuf[PKG_CONFIG_PATH_SZ];
	char **path;
	size_t count, iter = 0;
	const char *env_path;
	FILE *f;

	/* PKG_CONFIG_PATH has to take precedence */
	env_path = getenv("PKG_CONFIG_PATH");
	if (env_path)
	{
		count = path_split(env_path, &path);

		while (iter < count)
		{
			snprintf(locbuf, sizeof locbuf, "%s/%s" PKG_CONFIG_EXT, path[iter], name);
			free(path[iter]);

			if ((f = fopen(locbuf, "r")) != NULL)
				return parse_file(locbuf, f);

			iter++;
		}

		free(path);
	}

	if (!(flags & PKGF_ENV_ONLY))
	{
		snprintf(locbuf, sizeof locbuf, "%s/%s" PKG_CONFIG_EXT, PKG_DEFAULT_PATH, name);
		if ((f = fopen(locbuf, "r")) != NULL)
			return parse_file(locbuf, f);
	}

	return NULL;
}

/*
 * pkg_compare_version(a, b)
 *
 * compare versions using RPM version comparison rules as described in the LSB.
 */
int
pkg_compare_version(const char *a, const char *b)
{
	char oldch1, oldch2;
	char *str1, *str2;
	char *one, *two;
	int ret;
	bool isnum;

	/* optimization: if version matches then it's the same version. */
	if (!strcasecmp(a, b))
		return 0;

	str1 = LOCAL_COPY(a);
	str2 = LOCAL_COPY(b);

	one = str1;
	two = str2;

	while (*one && *two)
	{
		while (*one && !isalnum(*one))
			one++;
		while (*two && !isalnum(*two))
			two++;

		if (!(*one && *two))
			break;

		str1 = one;
		str2 = two;

		if (isdigit(*str1))
		{
			while (*str1 && isdigit(*str1))
				str1++;

			while (*str2 && isdigit(*str2))
				str2++;

			isnum = true;
		}
		else
		{
			while (*str1 && isalpha(*str1))
				str1++;

			while (*str2 && isalpha(*str2))
				str2++;

			isnum = false;
		}

		oldch1 = *str1;
		oldch2 = *str2;

		*str1 = '\0';
		*str2 = '\0';

		if (one == str1)
			return -1;

		if (two == str2)
			return (isnum ? 1 : -1);

		if (isnum)
		{
			int onelen, twolen;

			while (*one == '0')
				one++;

			while (*two == '0')
				two++;

			onelen = strlen(one);
			twolen = strlen(two);

			if (onelen > twolen)
				return 1;
			else if (twolen > onelen)
				return -1;
		}

		ret = strcmp(one, two);
		if (ret)
			return ret;

		*str1 = oldch1;
		*str2 = oldch2;

		one = str1;
		two = str2;
	}

	if ((!*one) && (!*two))
		return 0;

	if (!*one)
		return -1;

	return 1;
}

/*
 * pkg_get_comparator(pkgdep)
 *
 * returns the comparator used in a depgraph dependency node as a string.
 */
const char *
pkg_get_comparator(pkg_dependency_t *pkgdep)
{
	switch(pkgdep->compare)
	{
	case PKG_LESS_THAN:
		return "<";
	case PKG_GREATER_THAN:
		return ">";
	case PKG_LESS_THAN_EQUAL:
		return "<=";
	case PKG_GREATER_THAN_EQUAL:
		return ">=";
	case PKG_EQUAL:
		return "=";
	case PKG_NOT_EQUAL:
		return "!=";
	case PKG_ANY:
		return "(any)";
	default:
		return "???";
	}

	return "???";
}

/*
 * pkg_verify_dependency(pkgdep, flags)
 *
 * verify a pkg_dependency_t node in the depgraph.  if the dependency is solvable,
 * return the appropriate pkg_t object, else NULL.
 */
pkg_t *
pkg_verify_dependency(pkg_dependency_t *pkgdep, unsigned int flags)
{
	pkg_t *pkg;

	pkg = pkg_find(pkgdep->package, flags);
	if (pkg == NULL)
		return NULL;

	pkg->id = strdup(pkgdep->package);

	switch(pkgdep->compare)
	{
	case PKG_LESS_THAN:
		if (pkg_compare_version(pkg->version, pkgdep->version) < 0)
			return pkg;
		break;
	case PKG_GREATER_THAN:
		if (pkg_compare_version(pkg->version, pkgdep->version) > 0)
			return pkg;
		break;
	case PKG_LESS_THAN_EQUAL:
		if (pkg_compare_version(pkg->version, pkgdep->version) <= 0)
			return pkg;
		break;
	case PKG_GREATER_THAN_EQUAL:
		if (pkg_compare_version(pkg->version, pkgdep->version) >= 0)
			return pkg;
		break;
	case PKG_EQUAL:
		if (pkg_compare_version(pkg->version, pkgdep->version) == 0)
			return pkg;
		break;
	case PKG_NOT_EQUAL:
		if (pkg_compare_version(pkg->version, pkgdep->version) != 0)
			return pkg;
		break;
	case PKG_ANY:
	default:
		return pkg;
	}

	return NULL;
}

/*
 * pkg_verify_graph(root, depth)
 *
 * verify the graph dependency nodes are satisfiable by walking the tree using
 * pkg_traverse().
 */
void
pkg_verify_graph(pkg_t *root, int depth, unsigned int flags)
{
	pkg_traverse(root, NULL, NULL, depth, flags);
}

static inline void
pkg_walk_list(pkg_dependency_t *deplist,
	void (*pkg_traverse_func)(pkg_t *package, void *data),
	void *data,
	int maxdepth,
	unsigned int flags)
{
	pkg_dependency_t *node;

	foreach_list_entry(deplist, node)
	{
		pkg_t *pkgdep;

		if (*node->package == '\0')
			continue;

		pkgdep = pkg_verify_dependency(node, flags);
		if (pkgdep == NULL)
		{
			fprintf(stderr, "Package %s was not found in the pkg-config search path.\n", node->package);
			fprintf(stderr, "Perhaps you should add the directory containing `%s.pc'\n", node->package);
			fprintf(stderr, "to the PKG_CONFIG_PATH environment variable\n");
			fprintf(stderr, "No package '%s' found\n", node->package);
			exit(EXIT_FAILURE);
		}

		pkg_traverse(pkgdep, pkg_traverse_func, data, maxdepth - 1, flags);
	}
}

/*
 * pkg_traverse(root, pkg_traverse_func, data, maxdepth)
 *
 * walk the dependency graph up to maxdepth levels.  -1 means infinite recursion.
 */
void
pkg_traverse(pkg_t *root,
	void (*pkg_traverse_func)(pkg_t *package, void *data),
	void *data,
	int maxdepth,
	unsigned int flags)
{
	if (maxdepth == 0)
		return;

	pkg_walk_list(root->requires, pkg_traverse_func, data, maxdepth - 1, flags);

	if (flags & PKGF_SEARCH_PRIVATE)
		pkg_walk_list(root->requires_private, pkg_traverse_func, data, maxdepth - 1, flags);

	if (pkg_traverse_func != NULL)
		pkg_traverse_func(root, data);
}
