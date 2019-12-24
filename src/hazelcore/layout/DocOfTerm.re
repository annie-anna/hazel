open GeneralUtil;

[@deriving sexp]
type doc = Doc.t(TermTag.t);

let space = Doc.space;
let indent = Doc.indent;

let tag_Indent = Doc.tag(TermTag.Indent);
let tag_Padding = Doc.tag(TermTag.Padding);
let tag_DelimGroup = Doc.tag(TermTag.DelimGroup);
let tag_OpenChild = (~is_inline) =>
  Doc.tag(TermTag.mk_OpenChild(~is_inline, ()));
let tag_ClosedChild = (~is_inline) =>
  Doc.tag(TermTag.mk_ClosedChild(~is_inline, ()));
let tag_Step = step => Doc.tag(TermTag.Step(step));

let indent_and_align = (d: doc): doc =>
  Doc.(hcats([indent |> tag_Padding, align(d)]));

let doc_of_text = (~steps: CursorPath.steps, text: string): doc =>
  Doc.Text(text)
  |> Doc.tag(TermTag.mk_Text(~steps, ~length=String.length(text), ()));

let pad_operator =
    (~inline_padding as (left, right): (doc, doc), operator: doc): doc => {
  Doc.(
    choices([
      hcats([left |> tag_Padding, operator, right |> tag_Padding]),
      hcats([Linebreak, operator, right |> tag_Padding]),
    ])
  );
};

let doc_of_op = (~steps: CursorPath.steps, op_text: string, ()): doc =>
  Doc.Text(op_text) |> Doc.tag(TermTag.mk_Op(~steps, ()));

let doc_of_space_op = Doc.space |> Doc.tag(TermTag.SpaceOp);

let pad_child =
    (
      ~is_open: bool,
      ~inline_padding: (doc, doc)=(Doc.empty, Doc.empty),
      ~enforce_inline: bool,
      child: (~enforce_inline: bool) => doc,
    )
    : doc => {
  let (left, right) = inline_padding;
  let inline_choice =
    Doc.hcats([
      left |> tag_Padding,
      child(~enforce_inline=true),
      right |> tag_Padding,
    ]);
  let para_choice = indent_and_align(child(~enforce_inline=false));
  let (inline_choice, para_choice) =
    is_open
      ? (
        inline_choice |> tag_OpenChild(~is_inline=true),
        para_choice |> tag_OpenChild(~is_inline=false),
      )
      : (
        inline_choice |> tag_ClosedChild(~is_inline=true),
        para_choice |> tag_ClosedChild(~is_inline=false),
      );
  enforce_inline
    ? inline_choice
    : Doc.(
        choices([inline_choice, hcats([Linebreak, para_choice, Linebreak])])
      );
};

let pad_open_child = pad_child(~is_open=true);
let pad_closed_child = pad_child(~is_open=false);

let tag_Operand =
    (
      ~family: TermFamily.t,
      ~err: ErrStatus.t=NotInHole,
      ~verr: VarErrStatus.t=NotInVarHole,
    ) =>
  Doc.tag(
    TermTag.mk_Term(
      ~family,
      ~shape=TermShape.mk_Operand(~err, ~verr, ()),
      (),
    ),
  );

let doc_of_Unit = (~steps: CursorPath.steps, ()): doc =>
  DocOfDelim.doc(~path=(steps, 0), "()") |> tag_Operand(~family=Typ);

let doc_of_Num = (~steps: CursorPath.steps, ()): doc =>
  DocOfDelim.doc(~path=(steps, 0), LangUtil.typeN)
  |> tag_Operand(~family=Typ);

let doc_of_Bool = (~steps: CursorPath.steps, ()): doc =>
  DocOfDelim.doc(~path=(steps, 0), LangUtil.typeB)
  |> tag_Operand(~family=Typ);

let doc_of_EmptyHole =
    (~family: TermFamily.t, ~steps: CursorPath.steps, hole_lbl: string): doc =>
  DocOfDelim.empty_hole_doc(~steps, hole_lbl) |> tag_Operand(~family);

let doc_of_Wild = (~err: ErrStatus.t, ~steps: CursorPath.steps): doc =>
  DocOfDelim.doc(~path=(steps, 0), "_") |> tag_Operand(~family=Pat, ~err);

let doc_of_Var =
    (
      ~family: TermFamily.t,
      ~err: ErrStatus.t,
      ~verr: VarErrStatus.t,
      ~steps: CursorPath.steps,
      x: Var.t,
    )
    : doc =>
  doc_of_text(~steps, x) |> tag_Operand(~family, ~err, ~verr);

let doc_of_NumLit =
    (
      ~family: TermFamily.t,
      ~err: ErrStatus.t,
      ~steps: CursorPath.steps,
      n: int,
    )
    : doc =>
  doc_of_text(~steps, string_of_int(n)) |> tag_Operand(~family, ~err);

let doc_of_BoolLit =
    (
      ~family: TermFamily.t,
      ~err: ErrStatus.t,
      ~steps: CursorPath.steps,
      b: bool,
    )
    : doc =>
  doc_of_text(~steps, string_of_bool(b)) |> tag_Operand(~family, ~err);

let doc_of_ListNil =
    (~family: TermFamily.t, ~err: ErrStatus.t, ~steps: CursorPath.steps, ())
    : doc =>
  DocOfDelim.doc(~path=(steps, 0), "[]") |> tag_Operand(~family, ~err);

let doc_of_Parenthesized =
    (
      ~family: TermFamily.t,
      ~steps: CursorPath.steps,
      ~enforce_inline: bool,
      body: (~enforce_inline: bool) => doc,
    )
    : doc => {
  let open_group = DocOfDelim.open_Parenthesized(steps) |> tag_DelimGroup;
  let close_group = DocOfDelim.close_Parenthesized(steps) |> tag_DelimGroup;
  Doc.hcats([
    open_group,
    body |> pad_open_child(~enforce_inline),
    close_group,
  ])
  |> tag_Operand(~family);
};

let doc_of_List =
    (
      ~steps: CursorPath.steps,
      ~enforce_inline: bool,
      body: (~enforce_inline: bool) => doc,
    )
    : doc => {
  let open_group = DocOfDelim.open_List(steps) |> tag_DelimGroup;
  let close_group = DocOfDelim.close_List(steps) |> tag_DelimGroup;
  Doc.hcats([
    open_group,
    body |> pad_open_child(~enforce_inline),
    close_group,
  ])
  |> tag_Operand(~family=Typ);
};

let doc_of_Inj =
    (
      ~family: TermFamily.t,
      ~steps: CursorPath.steps,
      ~enforce_inline: bool,
      ~err: ErrStatus.t,
      ~inj_side: InjSide.t,
      body: (~enforce_inline: bool) => doc,
    )
    : doc => {
  let open_group = DocOfDelim.open_Inj(steps, inj_side) |> tag_DelimGroup;
  let close_group = DocOfDelim.close_Inj(steps) |> tag_DelimGroup;
  Doc.hcats([
    open_group,
    body |> pad_open_child(~enforce_inline),
    close_group,
  ])
  |> tag_Operand(~family, ~err);
};

let doc_of_Lam =
    (
      ~steps: CursorPath.steps,
      ~enforce_inline: bool,
      ~err: ErrStatus.t,
      p: (~enforce_inline: bool) => doc,
      ann: option((~enforce_inline: bool) => doc),
      body: (~enforce_inline: bool) => doc,
    )
    : doc => {
  let open_group = {
    let lam_delim = DocOfDelim.sym_Lam(steps);
    let open_delim = DocOfDelim.open_Lam(steps);
    let doc =
      switch (ann) {
      | None =>
        Doc.hcats([
          lam_delim,
          p |> pad_closed_child(~enforce_inline),
          open_delim,
        ])
      | Some(ann) =>
        let colon_delim = DocOfDelim.colon_Lam(steps);
        Doc.hcats([
          lam_delim,
          p |> pad_closed_child(~enforce_inline),
          colon_delim,
          ann |> pad_closed_child(~enforce_inline),
          open_delim,
        ]);
      };
    doc |> tag_DelimGroup;
  };
  let close_group = DocOfDelim.close_Lam(steps) |> tag_DelimGroup;
  Doc.hcats([
    open_group,
    body |> pad_open_child(~enforce_inline),
    close_group,
  ])
  |> tag_Operand(~family=Exp, ~err);
};

let doc_of_Case =
    (
      ~steps: CursorPath.steps,
      ~err: ErrStatus.t,
      scrut: (~enforce_inline: bool) => doc,
      rules: list(doc),
    )
    : doc => {
  let open_group = DocOfDelim.open_Case(steps) |> tag_DelimGroup;
  let close_group = DocOfDelim.close_Case(steps) |> tag_DelimGroup;
  Doc.(
    vseps(
      [
        choices([
          hseps([open_group, scrut(~enforce_inline=true)]),
          vseps([
            open_group,
            indent_and_align(scrut(~enforce_inline=false)),
          ]),
        ]),
        ...rules,
      ]
      @ [close_group],
    )
  )
  |> tag_Operand(~family=Exp, ~err);
};

let doc_of_Case_ann =
    (
      ~steps: CursorPath.steps,
      ~err: ErrStatus.t,
      scrut: (~enforce_inline: bool) => doc,
      rules: list(doc),
      ann: (~enforce_inline: bool) => doc,
    )
    : doc => {
  let open_group = DocOfDelim.open_Case(steps) |> tag_DelimGroup;
  let close_group = {
    let end_delim = DocOfDelim.close_Case_ann(steps);
    Doc.(
      choices([
        hseps([end_delim, ann(~enforce_inline=true)]),
        vseps([end_delim, indent_and_align(ann(~enforce_inline=false))]),
      ])
      |> tag_DelimGroup
    );
  };
  Doc.(
    vseps(
      [
        choices([
          hseps([open_group, scrut(~enforce_inline=true)]),
          vseps([
            open_group,
            indent_and_align(scrut(~enforce_inline=false)),
          ]),
        ]),
        ...rules,
      ]
      @ [close_group],
    )
  )
  |> tag_Operand(~family=Exp, ~err);
};

let doc_of_Rule =
    (
      ~steps: CursorPath.steps,
      p: (~enforce_inline: bool) => doc,
      clause: (~enforce_inline: bool) => doc,
    )
    : doc => {
  let delim_group =
    Doc.hcats([
      DocOfDelim.bar_Rule(steps),
      p
      |> pad_closed_child(
           ~inline_padding=(space, space),
           ~enforce_inline=false,
         ),
      DocOfDelim.arrow_Rule(steps),
    ]);
  Doc.(
    choices([
      hseps([delim_group, clause(~enforce_inline=true)]),
      vseps([delim_group, indent_and_align(clause(~enforce_inline=false))]),
    ])
  )
  |> Doc.tag(TermTag.mk_Term(~family=Exp, ~shape=Rule, ()));
};

let doc_of_LetLine =
    (
      ~steps: CursorPath.steps,
      p: (~enforce_inline: bool) => doc,
      ann: option((~enforce_inline: bool) => doc),
      def: (~enforce_inline: bool) => doc,
    )
    : doc => {
  let open_group = {
    let let_delim = DocOfDelim.let_LetLine(steps);
    let eq_delim = DocOfDelim.eq_LetLine(steps);
    let doc =
      switch (ann) {
      | None =>
        Doc.hcats([
          let_delim,
          p
          |> pad_closed_child(
               ~inline_padding=(space, space),
               ~enforce_inline=false,
             ),
          eq_delim,
        ])
      | Some(ann) =>
        let colon_delim = DocOfDelim.colon_LetLine(steps);
        Doc.hcats([
          let_delim,
          p
          |> pad_closed_child(
               ~inline_padding=(space, space),
               ~enforce_inline=false,
             ),
          colon_delim,
          ann
          |> pad_closed_child(
               ~inline_padding=(space, space),
               ~enforce_inline=false,
             ),
          eq_delim,
        ]);
      };
    doc |> tag_DelimGroup;
  };
  let close_group = DocOfDelim.in_LetLine(steps) |> tag_DelimGroup;
  Doc.hcats([
    open_group,
    def
    |> pad_open_child(~inline_padding=(space, space), ~enforce_inline=false),
    close_group,
  ]);
};

let rec doc_of_BinOp =
        (
          ~family: TermFamily.t,
          ~doc_of_operand:
             (~steps: CursorPath.steps, ~enforce_inline: bool, 'operand) => doc,
          ~doc_of_operator: (~steps: CursorPath.steps, 'operator) => doc,
          ~inline_padding_of_operator: 'operator => (doc, doc),
          ~steps: CursorPath.steps,
          ~enforce_inline: bool,
          ~seq: Seq.t('operand, 'operator),
          skel: Skel.t('operator),
        )
        : doc => {
  let go =
    doc_of_BinOp(
      ~family,
      ~doc_of_operand,
      ~doc_of_operator,
      ~inline_padding_of_operator,
      ~steps,
      ~enforce_inline,
      ~seq,
    );
  switch (skel) {
  | Placeholder(n) =>
    let operand = seq |> Seq.nth_operand(n);
    doc_of_operand(~steps=steps @ [n], ~enforce_inline, operand)
    |> tag_Step(n);
  | BinOp(err, op, skel1, skel2) =>
    let op_index = Skel.rightmost_tm_index(skel1) + Seq.length(seq);
    let op_doc =
      doc_of_operator(~steps=steps @ [op_index], op) |> tag_Step(op_index);
    let skel1_doc = go(skel1);
    let skel2_doc = go(skel2);
    Doc.hcats([
      skel1_doc |> tag_OpenChild(~is_inline=true),
      op_doc |> pad_operator(~inline_padding=inline_padding_of_operator(op)),
      skel2_doc |> tag_OpenChild(~is_inline=true),
    ])
    |> Doc.tag(TermTag.mk_Term(~family, ~shape=BinOp({err, op_index}), ()));
  };
};

let doc_of_NTuple =
    (
      ~family: TermFamily.t,
      ~get_tuple_elements: Skel.t('operator) => list(Skel.t('operator)),
      ~doc_of_operand:
         (~steps: CursorPath.steps, ~enforce_inline: bool, 'operand) => doc,
      ~doc_of_operator: (~steps: CursorPath.steps, 'operator) => doc,
      ~inline_padding_of_operator: 'operator => (doc, doc),
      ~steps: CursorPath.steps,
      ~enforce_inline: bool,
      OpSeq(skel, seq): OpSeq.t('operand, 'operator),
    )
    : doc => {
  let doc_of_BinOp =
    doc_of_BinOp(
      ~family,
      ~doc_of_operand,
      ~doc_of_operator,
      ~inline_padding_of_operator,
      ~enforce_inline,
      ~steps,
      ~seq,
    );
  switch (skel |> get_tuple_elements |> map_zip(doc_of_BinOp)) {
  | [] => failwith(__LOC__ ++ ": found empty tuple")
  | [(_, singleton_doc)] => singleton_doc
  | [(_, hd_doc), ...tl] =>
    let err =
      switch (skel) {
      | Placeholder(_) => assert(false)
      | BinOp(err, _, _, _) => err
      };
    let (doc, comma_indices) =
      tl
      |> List.fold_left(
           ((tuple, comma_indices), (elem, elem_doc)) => {
             // TODO multi-line tuples
             let comma_index =
               Skel.leftmost_tm_index(elem) - 1 + Seq.length(seq);
             let comma_doc =
               Doc.Text(",")
               |> Doc.tag(TermTag.mk_Op(~steps=steps @ [comma_index], ()))
               |> tag_Step(comma_index);
             let doc =
               Doc.hcats([
                 tuple,
                 comma_doc,
                 space,
                 elem_doc |> tag_OpenChild(~is_inline=true),
               ]);
             (doc, [comma_index, ...comma_indices]);
           },
           (hd_doc |> tag_OpenChild(~is_inline=true), []),
         );
    doc
    |> Doc.tag(
         TermTag.mk_Term(~family, ~shape=NTuple({comma_indices, err}), ()),
       );
  };
};

module Typ = {
  let is_space = _ => false;
  let is_comma = UHTyp.is_Prod;
  let is_zcomma = zop => zop |> ZTyp.erase_zoperator |> is_comma;
  let erase_zseq = ZTyp.erase_zseq;
  let get_tuple_elements = UHTyp.get_prod_elements;

  let inline_padding_of_operator =
    fun
    | UHTyp.Prod => (Doc.empty, Doc.space)
    | Arrow
    | Sum => (Doc.space, Doc.space);

  let doc_of_EmptyHole = doc_of_EmptyHole(~family=Typ);
  let doc_of_Parenthesized = doc_of_Parenthesized(~family=Typ);
  let doc_of_NTuple =
    doc_of_NTuple(
      ~family=Typ,
      ~get_tuple_elements,
      ~inline_padding_of_operator,
    );

  let rec doc_of_htyp =
          (~steps: CursorPath.steps, ~enforce_inline: bool, ty: HTyp.t): doc => {
    let doc_of_child = (~enforce_inline, step, ty) =>
      doc_of_htyp(~enforce_inline, ~steps=steps @ [step], ty)
      |> tag_Step(step);
    switch (ty) {
    | Hole => doc_of_EmptyHole(~steps, "?")
    | Unit => doc_of_Unit(~steps, ())
    | Num => doc_of_Num(~steps, ())
    | Bool => doc_of_Bool(~steps, ())
    | List(ty) =>
      Doc.hcats([
        Text("["),
        doc_of_child(0, ty) |> pad_open_child(~enforce_inline),
        Text("]"),
      ])
    | Arrow(ty1, ty2)
    | Prod(ty1, ty2)
    | Sum(ty1, ty2) =>
      let padded_op =
        switch (ty) {
        | Arrow(_) =>
          Doc.(
            hcats([
              choices([Linebreak, space]),
              Text(LangUtil.typeArrowSym ++ " "),
            ])
          )
        | Prod(_) => Doc.(hcats([Text(","), choices([Linebreak, space])]))
        | _sum => Doc.(hcats([choices([Linebreak, space]), Text("| ")]))
        };
      Doc.hcats([
        doc_of_child(~enforce_inline, 0, ty1),
        padded_op,
        doc_of_child(~enforce_inline, 1, ty2),
      ]);
    };
  };

  let rec doc =
          (~steps: CursorPath.steps, ~enforce_inline: bool, uty: UHTyp.t): doc =>
    switch (uty) {
    | T1(uty1) => doc_of_opseq(~steps, ~enforce_inline, uty1)
    | T0(uty0) => doc_of_operand(~steps, ~enforce_inline, uty0)
    }
  and doc_of_opseq =
      (~steps: CursorPath.steps, ~enforce_inline: bool, opseq: UHTyp.opseq)
      : doc =>
    doc_of_NTuple(
      ~doc_of_operand,
      ~doc_of_operator,
      ~steps,
      ~enforce_inline,
      opseq,
    )
  and doc_of_operator = (~steps: CursorPath.steps, op: UHTyp.operator): doc =>
    doc_of_op(~steps, UHTyp.string_of_operator(op), ())
  and doc_of_operand =
      (
        ~steps: CursorPath.steps,
        ~enforce_inline: bool,
        operand: UHTyp.operand,
      )
      : doc =>
    switch (operand) {
    | Hole => doc_of_EmptyHole(~steps, "?")
    | Unit => doc_of_Unit(~steps, ())
    | Num => doc_of_Num(~steps, ())
    | Bool => doc_of_Bool(~steps, ())
    | Parenthesized(body) =>
      let body = doc_of_child(~steps, ~child_step=0, body);
      doc_of_Parenthesized(~steps, ~enforce_inline, body);
    | List(body) =>
      let body = doc_of_child(~steps, ~child_step=0, body);
      doc_of_List(~steps, ~enforce_inline, body);
    }
  and doc_of_child = (~enforce_inline, ~steps, ~child_step, uty) =>
    doc(~steps=steps @ [child_step], ~enforce_inline, uty)
    |> tag_Step(child_step);
};

module Pat = {
  let inline_padding_of_operator =
    Doc.(
      fun
      | UHPat.Comma => (empty, space)
      | Space
      | Cons => (empty, empty)
    );

  let doc_of_EmptyHole = doc_of_EmptyHole(~family=Pat);
  let doc_of_NumLit = doc_of_NumLit(~family=Pat);
  let doc_of_BoolLit = doc_of_BoolLit(~family=Pat);
  let doc_of_ListNil = doc_of_ListNil(~family=Pat);
  let doc_of_Var = doc_of_Var(~family=Pat);
  let doc_of_Parenthesized = doc_of_Parenthesized(~family=Pat);
  let doc_of_Inj = doc_of_Inj(~family=Pat);
  let doc_of_NTuple =
    doc_of_NTuple(
      ~family=Pat,
      ~get_tuple_elements=UHPat.get_tuple_elements,
      ~inline_padding_of_operator,
    );

  let rec doc =
          (~steps: CursorPath.steps, ~enforce_inline: bool, p: UHPat.t): doc =>
    switch (p) {
    | P1(p1) => doc_of_opseq(~steps, ~enforce_inline, p1)
    | P0(p0) => doc_of_operand(~steps, ~enforce_inline, p0)
    }
  and doc_of_opseq =
      (~steps: CursorPath.steps, ~enforce_inline: bool, opseq: UHPat.opseq)
      : doc =>
    doc_of_NTuple(
      ~doc_of_operand,
      ~doc_of_operator,
      ~steps,
      ~enforce_inline,
      opseq,
    )
  and doc_of_operator = (~steps: CursorPath.steps, op: UHPat.operator): doc =>
    op |> UHPat.is_Space
      ? doc_of_space_op : doc_of_op(~steps, UHPat.string_of_operator(op), ())
  and doc_of_operand =
      (
        ~steps: CursorPath.steps,
        ~enforce_inline: bool,
        operand: UHPat.operand,
      )
      : doc =>
    switch (operand) {
    | EmptyHole(u) => doc_of_EmptyHole(~steps, string_of_int(u))
    | Wild(err) => doc_of_Wild(~err, ~steps)
    | Var(err, verr, x) => doc_of_Var(~steps, ~err, ~verr, x)
    | NumLit(err, n) => doc_of_NumLit(~err, ~steps, n)
    | BoolLit(err, b) => doc_of_BoolLit(~err, ~steps, b)
    | ListNil(err) => doc_of_ListNil(~err, ~steps, ())
    | Parenthesized(body) =>
      let body = doc_of_child(~steps, ~child_step=0, body);
      doc_of_Parenthesized(~steps, ~enforce_inline, body);
    | Inj(err, inj_side, body) =>
      let body = doc_of_child(~steps, ~child_step=0, body);
      doc_of_Inj(~err, ~steps, ~enforce_inline, ~inj_side, body);
    }
  and doc_of_child = (~enforce_inline, ~steps, ~child_step, p) =>
    doc(~steps=steps @ [child_step], ~enforce_inline, p)
    |> tag_Step(child_step);
};

module Exp = {
  let inline_padding_of_operator =
    Doc.(
      fun
      | UHExp.Space
      | Times
      | Cons => (empty, empty)
      | Plus
      | Minus
      | LessThan
      | GreaterThan
      | Equals
      | And
      | Or => (space, space)
      | Comma => (empty, space)
    );

  let doc_of_EmptyHole = doc_of_EmptyHole(~family=Exp);
  let doc_of_NumLit = doc_of_NumLit(~family=Exp);
  let doc_of_BoolLit = doc_of_BoolLit(~family=Exp);
  let doc_of_ListNil = doc_of_ListNil(~family=Exp);
  let doc_of_Var = doc_of_Var(~family=Exp);
  let doc_of_Parenthesized = doc_of_Parenthesized(~family=Exp);
  let doc_of_Inj = doc_of_Inj(~family=Exp);
  let doc_of_NTuple =
    doc_of_NTuple(
      ~family=Exp,
      ~get_tuple_elements=UHExp.get_tuple_elements,
      ~inline_padding_of_operator,
    );

  let tag_SubBlock = (~hd_index: int) =>
    Doc.tag(
      TermTag.mk_Term(
        ~family=Exp,
        ~shape=SubBlock({hd_index: hd_index}),
        (),
      ),
    );

  let rec doc =
          (~steps: CursorPath.steps, ~enforce_inline: bool, e: UHExp.t): doc =>
    switch (e) {
    | E2(e2) => enforce_inline ? Fail : doc_of_block(~steps, e2)
    | E1(e1) => doc_of_opseq(~steps, ~enforce_inline, e1)
    | E0(e0) => doc_of_operand(~steps, ~enforce_inline, e0)
    }
  and doc_of_block = (~steps: CursorPath.steps, block: UHExp.block): doc =>
    block
    |> List.mapi((i, line) =>
         doc_of_line(~steps=steps @ [i], line) |> tag_Step(i)
       )
    |> split_last
    |> (
      fun
      | None => failwith(__LOC__ ++ ": empty block")
      | Some((leading, concluding)) =>
        fold_right_i(
          ((i, hd_doc), tl_doc) =>
            Doc.vsep(hd_doc, tl_doc) |> tag_SubBlock(~hd_index=i),
          leading,
          concluding |> tag_SubBlock(~hd_index=UHExp.num_lines(block) - 1),
        )
    )
  and doc_of_line = (~steps: CursorPath.steps, line: UHExp.line): doc =>
    switch (line) {
    | EmptyLine => doc_of_text(~steps, "")
    | ExpLine(opseq) => doc_of_opseq(~steps, ~enforce_inline=false, opseq)
    | LetLine(p, ann, def) =>
      let p = Pat.doc_of_child(~steps, ~child_step=0, p);
      let ann =
        ann |> Opt.map(ann => Typ.doc_of_child(~steps, ~child_step=1, ann));
      let def = doc_of_child(~steps, ~child_step=2, def);
      doc_of_LetLine(~steps, p, ann, def);
    }
  and doc_of_opseq =
      (~steps: CursorPath.steps, ~enforce_inline: bool, opseq: UHExp.opseq)
      : doc =>
    doc_of_NTuple(
      ~doc_of_operand,
      ~doc_of_operator,
      ~steps,
      ~enforce_inline,
      opseq,
    )
  and doc_of_operator = (~steps: CursorPath.steps, op: UHExp.operator): doc =>
    op |> UHExp.is_Space
      ? doc_of_space_op : doc_of_op(~steps, UHExp.string_of_operator(op), ())
  and doc_of_operand =
      (
        ~steps: CursorPath.steps,
        ~enforce_inline: bool,
        operand: UHExp.operand,
      )
      : doc =>
    switch (operand) {
    | EmptyHole(u) => doc_of_EmptyHole(~steps, string_of_int(u))
    | Var(err, verr, x) => doc_of_Var(~err, ~verr, ~steps, x)
    | NumLit(err, n) => doc_of_NumLit(~err, ~steps, n)
    | BoolLit(err, b) => doc_of_BoolLit(~err, ~steps, b)
    | ListNil(err) => doc_of_ListNil(~err, ~steps, ())
    | Lam(err, p, ann, body) =>
      let p = Pat.doc_of_child(~steps, ~child_step=0, p);
      let ann =
        ann |> Opt.map(ann => Typ.doc_of_child(~steps, ~child_step=1, ann));
      let body = doc_of_child(~steps, ~child_step=2, body);
      doc_of_Lam(~err, ~steps, ~enforce_inline, p, ann, body);
    | Inj(err, inj_side, body) =>
      let body = doc_of_child(~steps, ~child_step=0, body);
      doc_of_Inj(~err, ~steps, ~enforce_inline, ~inj_side, body);
    | Parenthesized(body) =>
      let body = doc_of_child(~steps, ~child_step=0, body);
      doc_of_Parenthesized(~steps, ~enforce_inline, body);
    | Case(err, scrut, rules, ann) =>
      if (enforce_inline) {
        Fail;
      } else {
        let scrut = doc_of_child(~steps, ~child_step=0, scrut);
        let rules =
          rules
          |> List.mapi((i, rule) =>
               doc_of_rule(~steps=steps @ [1 + i], rule) |> tag_Step(1 + i)
             );
        switch (ann) {
        | None => doc_of_Case(~err, ~steps, scrut, rules)
        | Some(ann) =>
          let ann =
            Typ.doc_of_child(
              ~steps,
              ~child_step=1 + List.length(rules),
              ann,
            );
          doc_of_Case_ann(~err, ~steps, scrut, rules, ann);
        };
      }
    | ApPalette(_) => failwith("unimplemented: doc_of_exp/ApPalette")
    }
  and doc_of_rule =
      (~steps: CursorPath.steps, Rule(p, clause): UHExp.rule): doc => {
    let p = Pat.doc_of_child(~steps, ~child_step=0, p);
    let clause = doc_of_child(~steps, ~child_step=1, clause);
    doc_of_Rule(~steps, p, clause);
  }
  and doc_of_child = (~enforce_inline, ~steps, ~child_step, e) =>
    doc(~steps=steps @ [child_step], ~enforce_inline, e)
    |> tag_Step(child_step);
};
