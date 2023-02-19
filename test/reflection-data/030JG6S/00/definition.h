
// yaml: { flatten-field: value }
struct node {
	// yaml: { remove-prefix: nt_ }
	enum {
		nt_vnull,		// yaml: { null-selector: true }
		nt_vbool,
		nt_vinteger,
		nt_vfloat,
		nt_vstring,
		nt_vsequence,
		nt_vmapping,
	} node_type;
	// yaml: { field-auto-select: true }
	union {
		struct { } vnull;		// yaml: { match-null: true }
		_Bool vbool;
		int vinteger;
		float vfloat;
		char *vstring;
		struct node **vsequence;	// yaml: { match-seq: true }

		struct {
			struct node *key;
			struct node *value;
		} **vmapping;	// yaml: { key: key, match-map: true }
	} value;
};
