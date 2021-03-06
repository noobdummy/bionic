/* Extended attribute handling for AFS.  We use xattrs to get and set metadata
 * instead of providing pioctl().
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include "internal.h"

static const char afs_xattr_list[] =
	"afs.cell\0"
	"afs.fid\0"
	"afs.volume";

/*
 * Retrieve a list of the supported xattrs.
 */
ssize_t afs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	if (size == 0)
		return sizeof(afs_xattr_list);
	if (size < sizeof(afs_xattr_list))
		return -ERANGE;
	memcpy(buffer, afs_xattr_list, sizeof(afs_xattr_list));
	return sizeof(afs_xattr_list);
}

/*
 * Get the name of the cell on which a file resides.
 */
static int afs_xattr_get_cell(const struct xattr_handler *handler,
			      struct dentry *dentry,
			      struct inode *inode, const char *name,
			      void *buffer, size_t size)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_cell *cell = vnode->volume->cell;
	size_t namelen;

	namelen = cell->name_len;
	if (size == 0)
		return namelen;
	if (namelen > size)
		return -ERANGE;
	memcpy(buffer, cell->name, namelen);
	return namelen;
}

static const struct xattr_handler afs_xattr_afs_cell_handler = {
	.name	= "afs.cell",
	.get	= afs_xattr_get_cell,
};

/*
 * Get the volume ID, vnode ID and vnode uniquifier of a file as a sequence of
 * hex numbers separated by colons.
 */
static int afs_xattr_get_fid(const struct xattr_handler *handler,
			     struct dentry *dentry,
			     struct inode *inode, const char *name,
			     void *buffer, size_t size)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	char text[16 + 1 + 24 + 1 + 8 + 1];
	size_t len;

	/* The volume ID is 64-bit, the vnode ID is 96-bit and the
	 * uniquifier is 32-bit.
	 */
	len = sprintf(text, "%llx:", vnode->fid.vid);
	if (vnode->fid.vnode_hi)
		len += sprintf(text + len, "%x%016llx",
			       vnode->fid.vnode_hi, vnode->fid.vnode);
	else
		len += sprintf(text + len, "%llx", vnode->fid.vnode);
	len += sprintf(text + len, ":%x", vnode->fid.unique);

	if (size == 0)
		return len;
	if (len > size)
		return -ERANGE;
	memcpy(buffer, text, len);
	return len;
}

static const struct xattr_handler afs_xattr_afs_fid_handler = {
	.name	= "afs.fid",
	.get	= afs_xattr_get_fid,
};

/*
 * Get the name of the volume on which a file resides.
 */
static int afs_xattr_get_volume(const struct xattr_handler *handler,
			      struct dentry *dentry,
			      struct inode *inode, const char *name,
			      void *buffer, size_t size)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	const char *volname = vnode->volume->name;
	size_t namelen;

	namelen = strlen(volname);
	if (size == 0)
		return namelen;
	if (namelen > size)
		return -ERANGE;
	memcpy(buffer, volname, namelen);
	return namelen;
}

static const struct xattr_handler afs_xattr_afs_volume_handler = {
	.name	= "afs.volume",
	.get	= afs_xattr_get_volume,
};

const struct xattr_handler *afs_xattr_handlers[] = {
	&afs_xattr_afs_cell_handler,
	&afs_xattr_afs_fid_handler,
	&afs_xattr_afs_volume_handler,
	NULL
};
