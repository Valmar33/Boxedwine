/*
 *  Copyright (C) 2016  The BoxedWine Team
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#include "../../io/fsvirtualopennode.h"

class DevNull : public FsVirtualOpenNode {
public:
    DevNull(const BoxedPtr<FsNode>& node, U32 flags) : FsVirtualOpenNode(node, flags) {}
    virtual U32 readNative(U8* buffer, U32 len) {return 0;}
    virtual U32 writeNative(U8* buffer, U32 len) {return len;}
};

FsOpenNode* openDevNull(const BoxedPtr<FsNode>& node, U32 flags, U32 data) {
    return new DevNull(node, flags);
}
