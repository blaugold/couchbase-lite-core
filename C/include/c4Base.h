//
// c4Base.h
//
// Copyright (c) 2015 Couchbase, Inc All rights reserved.
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

#pragma once
#include "c4Compat.h"
#include "c4Error.h"
#include "c4Log.h"
#include "fleece/FLSlice.h"
#include <stdarg.h>

C4_ASSUME_NONNULL_BEGIN
C4API_BEGIN_DECLS


/** \defgroup Base  Data Types and Base Functions
 @{ */


#pragma mark - SLICES:


// (This is just renaming stuff from FLSlice.h ... feel free to use the FL names instead.)

typedef FLSlice         C4Slice;
typedef FLHeapSlice     C4HeapSlice;
typedef FLSliceResult   C4SliceResult;
typedef C4Slice         C4String;
typedef C4HeapSlice     C4HeapString;
typedef C4SliceResult   C4StringResult;

static C4INLINE C4Slice c4str(const char* C4NULLABLE str)   {return FLStr(str);}
#define C4STR(STR) FLSTR(STR)
#define kC4SliceNull kFLSliceNull

static inline bool c4SliceEqual(C4Slice a, C4Slice b)       {return FLSlice_Equal(a,b);}
static inline void c4slice_free(C4SliceResult s)            {FLSliceResult_Release(s);}


#pragma mark - COMMON TYPES:


/** A database sequence number, representing the order in which a revision was created. */
typedef uint64_t C4SequenceNumber;


/** A date/time representation used for document expiration (and in date/time queries.)
    Measured in milliseconds since the Unix epoch (1/1/1970, midnight UTC.) */
typedef int64_t C4Timestamp;


/** Client-defined metadata that can be associated with some objects like C4Database.
    (See \ref c4db_setExtraInfo, \ref c4db_getExtraInfo.)
    For example, if you have your own "Database" class, you could store a pointer to it in the
    ExtraInfo of the corresponding `C4Database` so you can map from it back to your object.

    The `destructor` callback is optional, but gives you a chance to clean up (e.g. release) your
    own object when the containing C4 object is freed. */
typedef struct C4ExtraInfo {
    void* C4NULLABLE pointer;                   /// Client-specific pointer; can be anything
    void (*C4NULLABLE destructor)(void *ptr);   /// Called when containing C4 object has been freed
} C4ExtraInfo;


/** A raw SHA-1 digest (20 bytes), used as the unique identifier of a blob. */
typedef struct C4BlobKey C4BlobKey;

/** A simple parsed-URL struct. */
typedef struct C4Address C4Address;

/** Opaque handle for an object that manages storage of blobs. */
typedef struct C4BlobStore C4BlobStore;

/** An X.509 certificate, or certificate signing request (CSR). */
typedef struct C4Cert C4Cert;

/** Opaque handle to a namespace of documents in an opened database. */
typedef struct C4Collection C4Collection;

/** Opaque handle to an opened database. */
typedef struct C4Database C4Database;

/** A collection-observer reference. */
typedef struct C4CollectionObserver C4CollectionObserver;

typedef C4CollectionObserver C4DatabaseObserver;    // TODO: Remove ASAP

/** Describes a version-controlled document. */
typedef struct C4Document C4Document;

/** A document-observer reference. */
typedef struct C4DocumentObserver C4DocumentObserver;

/** Opaque handle to a document enumerator. */
typedef struct C4DocEnumerator C4DocEnumerator;

/** An asymmetric key or key-pair (RSA, etc.) The private key may or may not be present. */
typedef struct C4KeyPair C4KeyPair;

/** A LiteCore network listener -- supports the REST API, replication, or both. */
typedef struct C4Listener C4Listener;

/** Opaque handle to a compiled query. */
typedef struct C4Query C4Query;

/** A query result enumerator. */
typedef struct C4QueryEnumerator C4QueryEnumerator;

/** A query-observer reference. */
typedef struct C4QueryObserver C4QueryObserver;

/** Contents of a raw document. */
typedef struct C4RawDocument C4RawDocument;

/** An open stream for reading data from a blob. */
typedef struct C4ReadStream C4ReadStream;

/** Opaque reference to a replicator. */
typedef struct C4Replicator C4Replicator;

/** Represents an open bidirectional stream of bytes or messages (typically a TCP socket.) */
typedef struct C4Socket C4Socket;

/** A group of callbacks that define the implementation of sockets. */
typedef struct C4SocketFactory C4SocketFactory;

/** An open stream for writing data to a blob. */
typedef struct C4WriteStream C4WriteStream;


#pragma mark - REFERENCE COUNTING:


// The actual functions behind c4xxx_retain / c4xxx_release; don't call directly
void* c4base_retain(void * C4NULLABLE obj) C4API;
void c4base_release(void * C4NULLABLE obj) C4API;

// These types are reference counted and have c4xxx_retain / c4xxx_release functions:
static inline C4Cert* C4NULLABLE
    c4cert_retain(C4Cert* C4NULLABLE r) C4API       {return (C4Cert*)c4base_retain(r);}
static inline C4KeyPair* C4NULLABLE
    c4keypair_retain(C4KeyPair* C4NULLABLE r) C4API {return (C4KeyPair*)c4base_retain(r);}
static inline C4Database* C4NULLABLE
    c4db_retain(C4Database* C4NULLABLE r) C4API     {return (C4Database*)c4base_retain(r);}
static inline C4Query* C4NULLABLE
    c4query_retain(C4Query* C4NULLABLE r) C4API     {return (C4Query*)c4base_retain(r);}

C4Document* C4NULLABLE
    c4doc_retain(C4Document* C4NULLABLE) C4API;
C4QueryEnumerator* C4NULLABLE
    c4queryenum_retain(C4QueryEnumerator* C4NULLABLE) C4API;

static inline void c4cert_release   (C4Cert* C4NULLABLE r) C4API       {c4base_release(r);}
static inline void c4keypair_release(C4KeyPair* C4NULLABLE r) C4API    {c4base_release(r);}
static inline void c4db_release     (C4Database* C4NULLABLE r) C4API   {c4base_release(r);}
static inline void c4query_release  (C4Query* C4NULLABLE r) C4API      {c4base_release(r);}

void               c4doc_release    (C4Document* C4NULLABLE) C4API;
void               c4queryenum_release(C4QueryEnumerator* C4NULLABLE) C4API;

// These types are _not_ ref-counted, but must be freed after use:
void c4dbobs_free        (C4DatabaseObserver* C4NULLABLE) C4API;
void c4docobs_free       (C4DocumentObserver* C4NULLABLE) C4API;
void c4enum_free         (C4DocEnumerator* C4NULLABLE) C4API;
void c4listener_free     (C4Listener* C4NULLABLE) C4API;
void c4queryobs_free     (C4QueryObserver* C4NULLABLE) C4API;
void c4raw_free          (C4RawDocument* C4NULLABLE) C4API;
void c4repl_free         (C4Replicator* C4NULLABLE) C4API;
void c4stream_close      (C4ReadStream* C4NULLABLE) C4API;
void c4stream_closeWriter(C4WriteStream* C4NULLABLE) C4API;


/** Returns the number of objects that have been created but not yet freed.
    This can be used as a debugging/testing tool to detect leaks; for example, capture the value
    at the start of a test, then call again at the end and compare. */
int c4_getObjectCount(void) C4API;

/** Logs information about object in memory. Useful for debugging when \ref c4_getObjectCount
    indicates there are leaks. (Note: In release builds this doesn't have much to say, because
    the instrumentation it needs is suppressed for performance purposes.) */
void c4_dumpInstances(void) C4API;


/** @} */


#pragma mark - INFO:


/** \defgroup Miscellaneous  Miscellaneous Functions
 @{ */


/** A string describing the version of LiteCore. Currently this just describes the Git branch and
    commit, in the form "Built from master branch, commit 0bc68f0d". */
C4StringResult c4_getBuildInfo(void) C4API;

/** A short version string. */
C4StringResult c4_getVersion(void) C4API;

/** Returns the current time, in _milliseconds_ since 1/1/1970. */
C4Timestamp c4_now(void) C4API;


/** Wiring call for platforms without discoverable temporary directories.  Simply sets the SQLite
    temp directory so that it can write its temporary files without error.  Several platforms need
    to do this, but not all need to use this API.  
    @param path The path to a writable directory for temporary files for SQLite
    @param err  If an error happens (e.g. it is an error to call this function twice), this parameter
                records it.
    @note  If you do call this function, you should call it before opening any databases.
    @note  Needless to say, the directory must already exist. */
bool c4_setTempDir(C4String path, C4Error* C4NULLABLE err) C4API;


/** Schedules a function to be called asynchronously on a background thread.
    @param task  A pointer to the function to run. It must take a single `void*` argument and
        return `void`. If it needs to return a value, it should call some other function you
        define and pass that value as a parameter.
    @param context  An arbitrary pointer that will be passed to the function. You can use this
        to provide state. Obviously, whatever this points to must remain valid until the
        future time when `task` is called. */
void c4_runAsyncTask(void (*task)(void*), void* C4NULLABLE context) C4API;


/** @} */


C4API_END_DECLS
C4_ASSUME_NONNULL_END
