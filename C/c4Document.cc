//
// C4Document.cc
//
// Copyright (c) 2017 Couchbase, Inc All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "c4Document.hh"
#include "c4BlobStore.hh"
#include "c4Internal.hh"
#include "c4ExceptionUtils.hh"
#include "c4Collection.hh"
#include "DatabaseImpl.hh"
#include "DocumentFactory.hh"
#include "LegacyAttachments.hh"
#include "RevID.hh"
#include "RevTree.hh"   // only for kDefaultRemoteID
#include "Base64.hh"
#include "Error.hh"
#include "SecureRandomize.hh"
#include "StringUtil.hh"
#include "Doc.hh"
#include "FleeceImpl.hh"
#include "DeepIterator.hh"

using namespace std;
using namespace fleece;
using namespace fleece::impl;
using namespace litecore;


C4Document::C4Document(C4Collection *collection, alloc_slice docID_)
:_collection(collection)
,_docID(move(docID_))
{
    DebugAssert(&_flags == &((C4Document_C*)this)->flags);
    DebugAssert((void*)&_docID == (void*)&((C4Document_C*)this)->docID);
    _extraInfo = { };
}


C4Document::~C4Document() {
    destructExtraInfo(_extraInfo);
}


C4Collection* C4Document::collection() const                {return _collection;}
C4Database* C4Document::database() const                    {return _collection->getDatabase();}
KeyStore& C4Document::keyStore() const                      {return _collection->keyStore();}


FLDict C4Document::getProperties() const noexcept {
    if (slice body = getRevisionBody(); body)
    return FLValue_AsDict(FLValue_FromData(body, kFLTrusted));
    else
        return nullptr;
}

alloc_slice C4Document::bodyAsJSON(bool canonical) const {
    if (!loadRevisionBody())
        error::_throw(error::NotFound);
    if (FLDict root = getProperties())
        return ((const Dict*)root)->toJSON(canonical);
    error::_throw(error::CorruptRevisionData);
}


void C4Document::setRevID(revid id) {
    if (id.size > 0)
        _revID = id.expanded();
    else
        _revID = nullslice;
}


bool C4Document::selectCurrentRevision() noexcept {
    // By default just fill in what we know about the current revision:
    if (exists()) {
        _selectedRevID = _revID;
        _selected.revID = _selectedRevID;
        _selected.sequence = _sequence;
        _selected.flags = revisionFlagsFromDocFlags(_flags);
    } else {
        clearSelectedRevision();
    }
    return false;
}


void C4Document::clearSelectedRevision() noexcept {
    _selectedRevID = nullslice;
    _selected.revID = _selectedRevID;
    _selected.flags = (C4RevisionFlags)0;
    _selected.sequence = 0;
}


alloc_slice C4Document::getSelectedRevIDGlobalForm() const {
    // By default just return the same revID
    DebugAssert(_selectedRevID == _selected.revID);
    return _selectedRevID;
}


void C4Document::requireValidDocID() const {
    if (!C4Document::isValidDocID(_docID))
        error::_throw(error::BadDocID, "Invalid docID \"%.*s\"", SPLAT(_docID));
}


#pragma mark - SAVING:


alloc_slice C4Document::createDocID() {
    char docID[C4Document::kGeneratedIDLength + 1];
    return alloc_slice(C4Document::generateID(docID, sizeof(docID)));
}


// Errors other than NotFound, Conflict and delta failures
// should be thrown as exceptions, in the C++ API.
static void throwIfUnexpected(const C4Error &inError, C4Error *outError) {
    if (outError)
        *outError = inError;
    if (inError.domain == LiteCoreDomain) {
        switch (inError.code) {
            case kC4ErrorNotFound:
            case kC4ErrorConflict:
            case kC4ErrorDeltaBaseUnknown:
            case kC4ErrorCorruptDelta:
                return; // don't throw these errors
        }
    }
    inError.raise();
}


Retained<C4Document> C4Document::update(slice revBody, C4RevisionFlags revFlags) {
    auto db = asInternal(database());
    db->mustBeInTransaction();
    db->validateRevisionBody(revBody);

    alloc_slice parentRev = _selectedRevID;
    C4DocPutRequest rq = {};
    rq.docID = _docID;
    rq.body = revBody;
    rq.revFlags = revFlags;
    rq.allowConflict = false;
    rq.history = (C4String*)&parentRev;
    rq.historyCount = 1;
    rq.save = true;

    // First the fast path: try to save directly via putNewRevision:
    if (loadRevisions()) {
        C4Error myErr;
        if (checkNewRev(parentRev, revFlags, false, &myErr)
            && putNewRevision(rq, &myErr)) {
            // Fast path succeeded!
            return this;
        } else if (myErr != C4Error{LiteCoreDomain, kC4ErrorConflict}) {
            // Something other than a conflict happened, so give up:
            myErr.raise();
        }
        // on conflict, fall through...
    }

    // MVCC prevented us from writing directly to the document. So instead, read-modify-write:
    C4Error myErr;
    Retained<C4Document> savedDoc = _collection->putDocument(rq, nullptr, &myErr);
    if (!savedDoc) {
        throwIfUnexpected(myErr, nullptr);
        savedDoc = nullptr;
    }
    return savedDoc;
}


bool C4Document::checkNewRev(slice parentRevID,
                             C4RevisionFlags rqFlags,
                             bool allowConflict,
                             C4Error *outError) noexcept
{
    int code = 0;
    if (parentRevID) {
        // Updating an existing revision; make sure it exists and is a leaf:
        if (!exists())
            code = kC4ErrorNotFound;
        else if (!selectRevision(parentRevID, false))
            code = allowConflict ? kC4ErrorNotFound : kC4ErrorConflict;
        else if (!allowConflict && !(_selected.flags & kRevLeaf))
            code = kC4ErrorConflict;
    } else {
        // No parent revision given:
        if (rqFlags & kRevDeleted) {
            // Didn't specify a revision to delete: NotFound or a Conflict, depending
            code = ((_flags & kDocExists) ?kC4ErrorConflict :kC4ErrorNotFound);
        } else if ((_flags & kDocExists) && !(_selected.flags & kRevDeleted)) {
            // If doc exists, current rev must be a deletion or there will be a conflict:
            code = kC4ErrorConflict;
        }
    }

    if (code) {
        c4error_return(LiteCoreDomain, code, nullslice, outError);
        return false;
    }
    return true;
}


#pragma mark - CONFLICTS:


void C4Document::resolveConflict(slice winningRevID,
                                 slice losingRevID,
                                 FLDict mergedProperties,
                                 C4RevisionFlags mergedFlags,
                                 bool pruneLosingBranch)
{
    alloc_slice mergedBody;
    if (mergedProperties) {
        auto enc = database()->sharedFleeceEncoder();
        FLEncoder_WriteValue(enc, (FLValue)mergedProperties);
        FLError flErr;
        mergedBody = FLEncoder_Finish(enc, &flErr);
        if (!mergedBody)
            error::_throw(error::Fleece, flErr);
    }
    return resolveConflict(winningRevID, losingRevID, mergedBody, mergedFlags);
}


#pragma mark - STATIC UTILITY FUNCTIONS:


[[noreturn]] void C4Document::failUnsupported() {
    error::_throw(error::UnsupportedOperation);
}


bool C4Document::isValidDocID(slice docID) noexcept {
    return docID.size >= 1 && docID.size <= 240 && docID[0] != '_'
    && isValidUTF8(docID) && hasNoControlCharacters(docID);
}


char* C4Document::generateID(char *outDocID, size_t bufferSize) noexcept {
    if (bufferSize < kGeneratedIDLength + 1)
        return nullptr;
    static const char kBase64[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    "0123456789-_";
    uint8_t r[kGeneratedIDLength - 1];
    SecureRandomize({r, sizeof(r)});
    outDocID[0] = '~';
    for (unsigned i = 0; i < sizeof(r); ++i)
    outDocID[i+1] = kBase64[r[i] % 64];
    outDocID[kGeneratedIDLength] = '\0';
    return outDocID;
}


bool C4Document::equalRevIDs(slice rev1, slice rev2) noexcept {
    try {
        if (rev1 == rev2)
            return true;
        revidBuffer buf1, buf2;
        return buf1.tryParse(rev1) && buf2.tryParse(rev2) && buf1.isEquivalentTo(buf2);
    }catchAndWarn()
    return false;
}


unsigned C4Document::getRevIDGeneration(slice revID) noexcept {
    try {
        return revidBuffer(revID).generation();
    }catchAndWarn()
    return 0;
}


C4RevisionFlags C4Document::revisionFlagsFromDocFlags(C4DocumentFlags docFlags) noexcept {
    C4RevisionFlags revFlags = 0;
    if (docFlags & kDocExists) {
        revFlags |= kRevLeaf;
        // For stupid historical reasons C4DocumentFlags and C4RevisionFlags aren't compatible
        if (docFlags & kDocDeleted)
            revFlags |= kRevDeleted;
        if (docFlags & kDocHasAttachments)
            revFlags |= kRevHasAttachments;
        if (docFlags & (C4DocumentFlags)DocumentFlags::kSynced)
            revFlags |= kRevKeepBody;
    }
    return revFlags;
}


C4Document* C4Document::containingValue(FLValue value) noexcept {
    return C4Collection::documentContainingValue(value);
}


bool C4Document::isOldMetaProperty(slice propertyName) noexcept {
    return legacy_attachments::isOldMetaProperty(propertyName);
}


bool C4Document::hasOldMetaProperties(FLDict dict) noexcept {
    return legacy_attachments::hasOldMetaProperties((const Dict*)dict);
}


alloc_slice C4Document::encodeStrippingOldMetaProperties(FLDict properties, FLSharedKeys sk) {
    return legacy_attachments::encodeStrippingOldMetaProperties((const Dict*)properties,
                                                                (SharedKeys*)sk);
}
