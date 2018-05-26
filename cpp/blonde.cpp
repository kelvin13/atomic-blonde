#include "sourcekitd.h"
#include <unordered_map>
#include <nan.h>

#define UID(symbol, string) uids. symbol = sourcekitd_uid_get_from_cstr(string)
#define SPECIES(key, id) species.insert({sourcekitd_uid_get_from_cstr(key), id})

namespace blonde 
{
    typedef struct 
    {
        uint16_t r, c;
    } latticepoint_t;
    
    typedef struct 
    {
        latticepoint_t start, end;
        uint8_t species;
        uint8_t _;
    } token_t;
    
    sourcekitd_object_t request;
    
    struct 
    {
        sourcekitd_uid_t key_request, 
            source_request_editor_open, 
            key_name, 
            key_sourcefile, 
            key_sourcetext, 
            key_enablesyntaxmap, 
            key_enablediagnostics, 
            key_enablesubstructure, 
            key_enablesyntaxtree, 
            
            key_syntaxmap, 
            key_diagnostics, 
            
            key_kind, 
            key_offset, 
            key_length;
    } uids;
    
    /*
    struct 
    {
        sourcekitd_uid_t source_diagnostic_severity_error,
            source_diagnostic_severity_warning, 
            source_diagnostic_severity_note;
    } diagnostics;
    */
    
    // we can probably speed this up 10x by implementing a custom single-layer hashmap 
    std::unordered_map<sourcekitd_uid_t, uint8_t> species;

    void create_uids(void) 
    {
        UID(key_request,                "key.request");
        UID(source_request_editor_open, "source.request.editor.open");
        UID(key_name,                   "key.name");
        UID(key_sourcefile,             "key.sourcefile");
        UID(key_sourcetext,             "key.sourcetext");
        UID(key_enablesyntaxmap,        "key.enablesyntaxmap");
        UID(key_enablediagnostics,      "key.enablediagnostics");
        UID(key_enablesubstructure,     "key.enablesubstructure");
        UID(key_enablesyntaxtree,       "key.enablesyntaxtree");
        
        UID(key_syntaxmap,              "key.syntaxmap");
        UID(key_diagnostics,            "key.diagnostics");
        
        UID(key_kind,                   "key.kind");
        UID(key_offset,                 "key.offset");
        UID(key_length,                 "key.length");
        
        //DIAGNOSTIC(source_diagnostic_severity_error,   "source.diagnostic.severity.error");
        //DIAGNOSTIC(source_diagnostic_severity_warning, "source.diagnostic.severity.warning");
        //DIAGNOSTIC(source_diagnostic_severity_note,    "source.diagnostic.severity.note");
        
        species.clear();
        SPECIES("source.lang.swift.syntaxtype.keyword",                     0);
        SPECIES("source.lang.swift.syntaxtype.identifier",                  1);
        SPECIES("source.lang.swift.syntaxtype.typeidentifier",              2);
        
        SPECIES("source.lang.swift.syntaxtype.buildconfig.keyword",         3);
        SPECIES("source.lang.swift.syntaxtype.buildconfig.id",              4);
        SPECIES("source.lang.swift.syntaxtype.pounddirective.keyword",      5);
        
        SPECIES("source.lang.swift.syntaxtype.attribute.id",                6);
        SPECIES("source.lang.swift.syntaxtype.attribute.builtin",           7);
        
        SPECIES("source.lang.swift.syntaxtype.number",                      8);
        SPECIES("source.lang.swift.syntaxtype.string",                      9);
        SPECIES("source.lang.swift.syntaxtype.string_interpolation_anchor", 10);
        
        SPECIES("source.lang.swift.syntaxtype.comment",                     11);
        SPECIES("source.lang.swift.syntaxtype.doccomment",                  12);
        SPECIES("source.lang.swift.syntaxtype.doccomment.field",            13);
        SPECIES("source.lang.swift.syntaxtype.comment.mark",                14);
        SPECIES("source.lang.swift.syntaxtype.comment.url",                 15);
        
        SPECIES("source.lang.swift.syntaxtype.placeholder",                 16);
        SPECIES("source.lang.swift.syntaxtype.objectliteral",               17);
    }
    
    void initialize(v8::FunctionCallbackInfo<v8::Value> const&)
    {
        create_uids();
        sourcekitd_initialize();
        
        request = sourcekitd_request_dictionary_create(nullptr, nullptr, 0);
        sourcekitd_request_dictionary_set_uid   (request, uids.key_request, uids.source_request_editor_open);
        sourcekitd_request_dictionary_set_int64 (request, uids.key_enablesyntaxmap, 1);
        sourcekitd_request_dictionary_set_int64 (request, uids.key_enablediagnostics, 0);
        sourcekitd_request_dictionary_set_int64 (request, uids.key_enablesubstructure, 0);
        sourcekitd_request_dictionary_set_int64 (request, uids.key_enablesyntaxtree, 0);    
    }
    
    void highlight(v8::FunctionCallbackInfo<v8::Value> const& frame)
    {
        v8::Isolate* isolate = v8::Isolate::GetCurrent();
        
        if (frame.Length() != 1 || !frame[0] -> IsString())
        {
            isolate -> ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8(isolate, "invalid arguments")));
        }
        
        // reason 85 why C++ is terrible:
        Nan::Utf8String retain(frame[0]);
        char const* const source = (char const*) *retain;
        
        sourcekitd_request_dictionary_set_string(request, uids.key_name, "atomic blonde");
        sourcekitd_request_dictionary_set_string(request, uids.key_sourcetext, source);
        
        sourcekitd_response_t const response = sourcekitd_send_request_sync(request);
        
        if (sourcekitd_response_is_error(response))
        {
            isolate -> ThrowException(v8::Exception::Error(
                v8::String::NewFromUtf8(isolate, "invalid response")));
        }
        
        sourcekitd_variant_t  const dict     = sourcekitd_response_get_value(response);
        sourcekitd_variant_t  const syntax   = sourcekitd_variant_dictionary_get_value(dict, uids.key_syntaxmap);
        
        size_t const count    = sourcekitd_variant_array_get_count(syntax);
        
        token_t* const tokens = new token_t[count];
        uint16_t row     = 0;
        uint64_t lastnl  = 0, // 1 past the offset of last newline character seen
                 current = 0; // current character
        for (size_t i = 0; i < count; ++i)
        {
            sourcekitd_variant_t const token = sourcekitd_variant_array_get_value(syntax, i);
            uint64_t const start = sourcekitd_variant_dictionary_get_int64(token, uids.key_offset),
                           count = sourcekitd_variant_dictionary_get_int64(token, uids.key_length);
            
            // convert 1D indices to 2D row–column indices 
            while (current < start) 
            {
                if (source[current++] == '\n')
                {
                    ++row;
                    lastnl = current;
                }
            }
            
            tokens[i].start = {row, static_cast<uint16_t>(start - lastnl)};
            
            while (current < start + count) 
            {
                if (source[current++] == '\n')
                {
                    ++row;
                    lastnl = current;
                }
            }
            
            tokens[i].end     = {row, static_cast<uint16_t>(start + count - lastnl)};
            tokens[i].species = species[sourcekitd_variant_dictionary_get_uid(token, uids.key_kind)];
        }
        
        sourcekitd_response_dispose(response);
        // crashes happen when we try to use NewBuffer ???
        frame.GetReturnValue().Set(Nan::CopyBuffer((char*) tokens, count * sizeof(token_t)).ToLocalChecked());
        delete[] tokens;
    }
    
    void deinitialize(v8::FunctionCallbackInfo<v8::Value> const&)
    {
        sourcekitd_request_release(request);
        sourcekitd_shutdown();
    }
}

void Init(v8::Handle<v8::Object> exports)
{
    NODE_SET_METHOD(exports, "initialize", blonde::initialize);
    NODE_SET_METHOD(exports, "highlight", blonde::highlight);
    NODE_SET_METHOD(exports, "deinitialize", blonde::deinitialize);
}

NODE_MODULE(blonde, Init)
