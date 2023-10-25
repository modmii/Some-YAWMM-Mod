#include <mxml.h>
#include <malloc.h>
#include <string.h>

#include "appmetadata.h"

static const char* GetStringValue(mxml_node_t* node, const char* element)
{
	mxml_node_t* elementNode = mxmlFindElement(node, node, element, NULL, NULL, MXML_DESCEND_FIRST);
	if (!elementNode) return NULL;

	mxml_node_t* current = mxmlGetFirstChild(elementNode);

	while (current && mxmlGetType(current) != MXML_OPAQUE)
		current = mxmlWalkNext(current, elementNode, MXML_NO_DESCEND);

	if (current)
		return mxmlGetOpaque(current);
	else
		return NULL;

}

static const char* GetArgumentValue(mxml_node_t* node)
{
	if (!node) return NULL;

	mxml_node_t* current = mxmlGetFirstChild(node);

	while (current && mxmlGetType(current) != MXML_OPAQUE)
		current = mxmlWalkNext(current, node, MXML_NO_DESCEND);

	if (current)
		return mxmlGetOpaque(current);
	else
		return NULL;
}

struct MetaData* LoadMetaData(const char* path)
{
	FILE* f = fopen(path, "rb");
	
	if (f == NULL)
		return NULL;
	
	mxml_node_t* meta = mxmlLoadFile(NULL, f, MXML_OPAQUE_CALLBACK);
	fclose(f);

	if (!meta)
		return NULL;

	mxml_node_t* app = mxmlFindElement(meta, meta, "app", NULL, NULL, MXML_DESCEND_FIRST);

	if (!app)
	{
		mxmlDelete(meta);
		return NULL;
	}

	struct MetaData* metaData = (struct MetaData*)malloc(sizeof(struct MetaData));
	if (!metaData)
	{
		mxmlDelete(meta);
		return NULL;
	}

	memset(metaData, 0, sizeof(struct MetaData));

	metaData->name = strdup(GetStringValue(app, "name"));
	metaData->coder = strdup(GetStringValue(app, "coder"));
	metaData->version = strdup(GetStringValue(app, "version"));
	metaData->shortDescription = strdup(GetStringValue(app, "short_description"));
	metaData->longDescription = strdup(GetStringValue(app, "long_description"));

	char release[20];
	memset(release, 0, sizeof(release));
	snprintf(release, sizeof(release), "%s", GetStringValue(app, "release_date"));
	
	metaData->releaseDate = NULL;
	if (strlen(release) == 14)
	{
		snprintf(release, sizeof(release), "%c%c/%c%c/%c%c%c%c", release[4], release[5], release[6], release[7], release[0], release[1], release[2], release[3]);
		metaData->releaseDate = strdup(release);
	}	
	else if (strlen(release) == 12)
	{
		snprintf(release, sizeof(release), "%c%c/%c%c%c%c", release[4], release[5], release[0], release[1], release[2], release[3]);
		metaData->releaseDate = strdup(release);
	}

	metaData->releaseDate = strdup(release);
	mxmlDelete(meta);

	return metaData;
}

void FreeMetaData(struct MetaData* metaData)
{
	free(metaData->name);
	free(metaData->coder);
	free(metaData->version);
	free(metaData->shortDescription);
	free(metaData->longDescription);
	free(metaData->releaseDate);
	free(metaData);
}

char* LoadArguments(const char* path, u16* length)
{
	FILE* f = fopen(path, "rb");

	if (f == NULL)
		return NULL;

	mxml_node_t* meta = mxmlLoadFile(NULL, f, MXML_OPAQUE_CALLBACK);

	fclose(f);

	if (!meta)
		return NULL;
	
	mxml_node_t* app = mxmlFindElement(meta, meta, "app", NULL, NULL, MXML_DESCEND_FIRST);
	
	if (!app)
	{
		mxmlDelete(meta);
		return NULL;
	}

	mxml_node_t* arguments = mxmlFindElement(app, app, "arguments", NULL, NULL, MXML_DESCEND_FIRST);

	if (!arguments)
	{
		mxmlDelete(meta);
		return NULL;
	}
	
	mxml_node_t* arg;
	u16 size = 0;
	
	for (arg = mxmlFindElement(arguments, arguments, "arg", NULL, NULL, MXML_DESCEND_FIRST); arg != NULL; arg = mxmlFindElement(arg, arguments, "arg", NULL, NULL, MXML_NO_DESCEND))
	{
		const char* current = GetArgumentValue(arg);

		if (current)
		{
			if (size > 0)
				size++;

			size += strlen(current);
		}
	}

	if (size == 0 || size > 1024)
	{
		mxmlDelete(meta);
		return NULL;
	}

	size++;
	char* argStr = malloc(size);
	size = 0;

	for (arg = mxmlFindElement(arguments, arguments, "arg", NULL, NULL, MXML_DESCEND_FIRST); arg != NULL; arg = mxmlFindElement(arg, arguments, "arg", NULL, NULL, MXML_NO_DESCEND))
	{
		const char* current = GetArgumentValue(arg);

		if (current)
		{
			if (size > 0)
			{
				argStr[size] = 0;
				size++;
			}
				
			strcpy(argStr + size, current);
			size += strlen(current);
		}
	}

	mxmlDelete(meta);
	
	argStr[size] = 0;
	*length = size;

	return argStr;
}
