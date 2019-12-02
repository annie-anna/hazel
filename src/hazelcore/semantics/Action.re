let _TEST_PERFORM = false;
open GeneralUtil;
open Sexplib.Std;

[@deriving sexp]
type operator_shape =
  | SMinus
  | SPlus
  | STimes
  | SLessThan
  | SGreaterThan
  | SEquals
  | SSpace
  | SComma
  | SArrow
  | SVBar
  | SCons
  | SAnd
  | SOr;

let exp_op_of = (os: operator_shape): option(UHExp.operator) =>
  switch (os) {
  | SPlus => Some(Plus)
  | SMinus => Some(Minus)
  | STimes => Some(Times)
  | SLessThan => Some(LessThan)
  | SGreaterThan => Some(GreaterThan)
  | SEquals => Some(Equals)
  | SSpace => Some(Space)
  | SComma => Some(Comma)
  | SCons => Some(Cons)
  | SAnd => Some(And)
  | SOr => Some(Or)
  | SArrow
  | SVBar => None
  };

let op_shape_of_exp_op = (op: UHExp.operator): operator_shape =>
  switch (op) {
  | Minus => SMinus
  | Plus => SPlus
  | Times => STimes
  | LessThan => SLessThan
  | GreaterThan => SGreaterThan
  | Equals => SEquals
  | Space => SSpace
  | Comma => SComma
  | Cons => SCons
  | And => SAnd
  | Or => SOr
  };

[@deriving sexp]
type shape =
  | SParenthesized
  /* type shapes */
  | SNum
  | SBool
  | SList
  /* expression shapes */
  | SAsc
  | SVar(string, CursorPosition.t)
  | SNumLit(int, CursorPosition.t)
  | SLam
  | SListNil
  | SInj(InjSide.t)
  | SLet
  | SLine
  | SCase
  | SOp(operator_shape)
  | SApPalette(PaletteName.t)
  /* pattern-only shapes */
  | SWild;

[@deriving sexp]
type t =
  | MoveTo(CursorPath.t)
  | MoveToBefore(CursorPath.steps)
  | MoveLeft
  | MoveRight
  | MoveToNextHole
  | MoveToPrevHole
  | UpdateApPalette(SpliceGenMonad.t(SerializedModel.t))
  | Delete
  | Backspace
  | Construct(shape);

type result('success) =
  | Succeeded('success)
  | CursorEscaped(Side.t)
  | Failed;

let _cursor_escaped_zopseq =
    (
      ~move_cursor_left:
         ZOpSeq.t('operand, 'operator, 'zoperand, 'zoperator) =>
         option(ZOpSeq.t('operand, 'operator, 'zoperand, 'zoperator)),
      ~move_cursor_right:
         ZOpSeq.t('operand, 'operator, 'zoperand, 'zoperator) =>
         option(ZOpSeq.t('operand, 'operator, 'zoperand, 'zoperator)),
      ~mk_result:
         ZOpSeq.t('operand, 'operator, 'zoperand, 'zoperator) =>
         result('success),
      side: Side.t,
      zopseq: ZOpSeq.t('operand, 'operator, 'zoperand, 'zoperator),
    )
    : result('success) =>
  switch (side) {
  | Before =>
    switch (zopseq |> move_cursor_left) {
    | None => CursorEscaped(Before)
    | Some(zopseq) => mk_result(zopseq)
    }
  | After =>
    switch (zopseq |> move_cursor_right) {
    | None => CursorEscaped(After)
    | Some(zopseq) => mk_result(zopseq)
    }
  };

module Typ = {
  let operator_of_shape = (os: operator_shape): option(UHTyp.operator) =>
    switch (os) {
    | SArrow => Some(Arrow)
    | SComma => Some(Prod)
    | SVBar => Some(Sum)
    | SMinus
    | SPlus
    | STimes
    | SAnd
    | SOr
    | SLessThan
    | SGreaterThan
    | SEquals
    | SSpace
    | SCons => None
    };

  let shape_of_operator = (op: UHTyp.operator): operator_shape =>
    switch (op) {
    | Arrow => SArrow
    | Prod => SComma
    | Sum => SVBar
    };

  let mk_ZOpSeq =
    ZOpSeq.mk(
      ~associate=Associator.associate_ty,
      ~erase_zoperand=ZTyp.erase_zoperand,
      ~erase_zoperator=ZTyp.erase_zoperator,
    );

  let cursor_escaped_zopseq =
    _cursor_escaped_zopseq(
      ~move_cursor_left=ZTyp.move_cursor_left_zopseq,
      ~move_cursor_right=ZTyp.move_cursor_right_zopseq,
      ~mk_result=z =>
      Succeeded(ZTyp.ZT1(z))
    );

  let construct_operator =
      (
        operator: UHTyp.operator,
        zoperand: ZTyp.zoperand,
        (prefix, suffix): ZTyp.operand_surround,
      )
      : ZTyp.zopseq => {
    let operand = zoperand |> ZTyp.erase_zoperand;
    let (zoperand, surround) =
      if (ZTyp.is_before_zoperand(zoperand)) {
        let zoperand = UHTyp.Hole |> ZTyp.place_before_operand;
        let new_suffix = Seq.A(operator, S(operand, suffix));
        (zoperand, (prefix, new_suffix));
      } else {
        let zoperand = UHTyp.Hole |> ZTyp.place_before_operand;
        let new_prefix = Seq.A(operator, S(operand, prefix));
        (zoperand, (new_prefix, suffix));
      };
    mk_ZOpSeq(ZOperand(zoperand, surround));
  };

  let rec perform = (a: t, zty: ZTyp.t): result(ZTyp.t) =>
    switch (zty) {
    | ZT1(zty1) => perform_opseq(a, zty1)
    | ZT0(zty0) => perform_operand(a, zty0)
    }
  and perform_opseq =
      (a: t, ZOpSeq(skel, zseq) as zopseq: ZTyp.zopseq): result(ZTyp.t) =>
    switch (a, zseq) {
    /* Invalid actions at the type level */
    | (
        UpdateApPalette(_) |
        Construct(
          SAsc | SLet | SLine | SVar(_) | SLam | SNumLit(_) | SListNil |
          SInj(_) |
          SCase |
          SApPalette(_) |
          SWild,
        ),
        _,
      )
    /* Invalid cursor positions */
    | (_, ZOperator((OnText(_) | OnDelim(_), _), _)) => Failed

    /* Movement */
    | (MoveTo(path), _) =>
      switch (CursorPath.Typ.follow_opseq(path, zopseq |> ZTyp.erase_zopseq)) {
      | None => Failed
      | Some(zopseq) => Succeeded(ZT1(zopseq))
      }
    | (MoveToBefore(steps), _) =>
      switch (
        CursorPath.Typ.follow_steps_opseq(
          ~side=Before,
          steps,
          zopseq |> ZTyp.erase_zopseq,
        )
      ) {
      | None => Failed
      | Some(zopseq) => Succeeded(ZT1(zopseq))
      }
    | (MoveToPrevHole, _) =>
      switch (
        CursorPath.prev_hole_steps(CursorPath.Typ.holes_zopseq(zopseq, []))
      ) {
      | None => Failed
      | Some(steps) => perform_opseq(MoveToBefore(steps), zopseq)
      }
    | (MoveToNextHole, _) =>
      switch (
        CursorPath.next_hole_steps(CursorPath.Typ.holes_zopseq(zopseq, []))
      ) {
      | None => Failed
      | Some(steps) => perform_opseq(MoveToBefore(steps), zopseq)
      }
    | (MoveLeft, _) =>
      zopseq
      |> ZTyp.move_cursor_left_zopseq
      |> Opt.map_default(~default=CursorEscaped(Before), z =>
           Succeeded(ZTyp.ZT1(z))
         )
    | (MoveRight, _) =>
      zopseq
      |> ZTyp.move_cursor_right_zopseq
      |> Opt.map_default(~default=CursorEscaped(After), z =>
           Succeeded(ZTyp.ZT1(z))
         )

    /* Deletion */

    | (Delete, ZOperator((OnOp(After), _), _)) =>
      cursor_escaped_zopseq(After, zopseq)
    | (Backspace, ZOperator((OnOp(Before), _), _)) =>
      cursor_escaped_zopseq(Before, zopseq)

    /* Delete before operator == Backspace after operator */
    | (Delete, ZOperator((OnOp(Before), op), surround)) =>
      perform_opseq(
        Backspace,
        ZOpSeq(skel, ZOperator((OnOp(After), op), surround)),
      )
    /* ... + [k-2] + [k-1] +<| [k] + ...   ==>   ... + [k-2] + [k-1]| + ...
     * (for now until we have proper type constructors) */
    | (Backspace, ZOperator((OnOp(After), op), (prefix, suffix))) =>
      let S(prefix_hd, new_prefix) = prefix;
      let zoperand = prefix_hd |> ZTyp.place_after_operand;
      let S(_, new_suffix) = suffix;
      Succeeded(
        ZT1(mk_ZOpSeq(ZOperand(zoperand, (new_prefix, new_suffix)))),
      );

    /* Construction */
    /* construction on operators becomes movement... */
    | (Construct(SOp(SSpace)), ZOperator(_)) =>
      perform_opseq(MoveRight, zopseq)
    /* ...or construction after movement */
    | (Construct(_) as a, ZOperator(zoperator, _)) =>
      let move_cursor =
        ZTyp.is_before_zoperator(zoperator)
          ? ZTyp.move_cursor_left_zopseq : ZTyp.move_cursor_right_zopseq;
      switch (zopseq |> move_cursor) {
      | None => Failed
      | Some(zopseq) => perform_opseq(a, zopseq)
      };

    /* Space becomes movement until we have proper type constructors */
    | (Construct(SOp(SSpace)), ZOperand(zoperand, _))
        when ZTyp.is_after_zoperand(zoperand) =>
      perform_opseq(MoveRight, zopseq)

    | (Construct(SOp(os)), ZOperand(CursorT(_) as zoperand, surround)) =>
      switch (operator_of_shape(os)) {
      | None => Failed
      | Some(op) =>
        Succeeded(ZT1(construct_operator(op, zoperand, surround)))
      }

    /* Zipper */
    | (_, ZOperand(zoperand, (prefix, suffix) as surround)) =>
      switch (perform_operand(a, zoperand)) {
      | Failed => Failed
      | CursorEscaped(side) => cursor_escaped_zopseq(side, zopseq)
      | Succeeded(ZT0(zoperand)) =>
        Succeeded(ZT1(mk_ZOpSeq(ZOperand(zoperand, surround))))
      | Succeeded(ZT1(ZOpSeq(_, zseq))) =>
        switch (zseq) {
        | ZOperand(zoperand, (inner_prefix, inner_suffix)) =>
          let new_prefix = Seq.affix_affix(inner_prefix, prefix);
          let new_suffix = Seq.affix_affix(inner_suffix, suffix);
          Succeeded(
            ZT1(mk_ZOpSeq(ZOperand(zoperand, (new_prefix, new_suffix)))),
          );
        | ZOperator(zoperator, (inner_prefix, inner_suffix)) =>
          let new_prefix = Seq.seq_affix(inner_prefix, prefix);
          let new_suffix = Seq.seq_affix(inner_suffix, suffix);
          Succeeded(
            ZT1(mk_ZOpSeq(ZOperator(zoperator, (new_prefix, new_suffix)))),
          );
        }
      }
    }
  and perform_operand = (a: t, zoperand: ZTyp.zoperand): result(ZTyp.t) =>
    switch (a, zoperand) {
    /* Invalid actions at the type level */
    | (
        UpdateApPalette(_) |
        Construct(
          SAsc | SLet | SLine | SVar(_) | SLam | SNumLit(_) | SListNil |
          SInj(_) |
          SCase |
          SApPalette(_) |
          SWild,
        ),
        _,
      ) =>
      Failed

    /* Invalid cursor positions */
    | (_, CursorT(OnText(_) | OnOp(_), _)) => Failed
    | (_, CursorT(cursor, operand))
        when !ZTyp.is_valid_cursor_operand(cursor, operand) =>
      Failed

    /* Movement */
    | (MoveTo(path), _) =>
      switch (
        CursorPath.Typ.follow_operand(path, zoperand |> ZTyp.erase_zoperand)
      ) {
      | None => Failed
      | Some(zoperand) => Succeeded(ZT0(zoperand))
      }
    | (MoveToBefore(steps), _) =>
      switch (
        CursorPath.Typ.follow_steps_operand(
          ~side=Before,
          steps,
          zoperand |> ZTyp.erase_zoperand,
        )
      ) {
      | None => Failed
      | Some(zoperand) => Succeeded(ZT0(zoperand))
      }
    | (MoveToPrevHole, _) =>
      switch (
        CursorPath.prev_hole_steps(
          CursorPath.Typ.holes_zoperand(zoperand, []),
        )
      ) {
      | None => Failed
      | Some(steps) => perform_operand(MoveToBefore(steps), zoperand)
      }
    | (MoveToNextHole, _) =>
      switch (
        CursorPath.next_hole_steps(
          CursorPath.Typ.holes_zoperand(zoperand, []),
        )
      ) {
      | None => Failed
      | Some(steps) => perform_operand(MoveToBefore(steps), zoperand)
      }
    | (MoveLeft, _) =>
      zoperand
      |> ZTyp.move_cursor_left_zoperand
      |> Opt.map_default(~default=CursorEscaped(Before), zoperand =>
           Succeeded(ZTyp.ZT0(zoperand))
         )
    | (MoveRight, _) =>
      zoperand
      |> ZTyp.move_cursor_right_zoperand
      |> Opt.map_default(~default=CursorEscaped(After), zoperand =>
           Succeeded(ZTyp.ZT0(zoperand))
         )

    /* Backspace and Delete */

    /* ( _ <|)   ==>   ( _| ) */
    | (Backspace, CursorT(OnDelim(_, Before), _)) =>
      zoperand |> ZTyp.is_before_zoperand
        ? CursorEscaped(Before) : perform_operand(MoveLeft, zoperand)
    /* (|> _ )   ==>   ( |_ ) */
    | (Delete, CursorT(OnDelim(_, After), _)) =>
      zoperand |> ZTyp.is_after_zoperand
        ? CursorEscaped(After) : perform_operand(MoveRight, zoperand)

    /* Delete before delimiter == Backspace after delimiter */
    | (Delete, CursorT(OnDelim(k, Before), operand)) =>
      perform_operand(Backspace, CursorT(OnDelim(k, After), operand))

    | (Backspace, CursorT(OnDelim(_, After), Hole)) =>
      Succeeded(ZT0(ZTyp.place_before_operand(Hole)))

    | (Backspace, CursorT(OnDelim(_, After), Unit | Num | Bool)) =>
      Succeeded(ZT0(ZTyp.place_before_operand(Hole)))

    /* ( _ )<|  ==>  _| */
    /* (<| _ )  ==>  |_ */
    | (
        Backspace,
        CursorT(OnDelim(k, After), Parenthesized(body) | List(body)),
      ) =>
      let place_cursor = k == 0 ? ZTyp.place_before : ZTyp.place_after;
      Succeeded(body |> place_cursor);

    /* Construction */

    | (Construct(SOp(SSpace)), CursorT(OnDelim(_, After), _)) =>
      perform_operand(MoveRight, zoperand)
    | (Construct(_) as a, CursorT(OnDelim(_, side), _))
        when
          !ZTyp.is_before_zoperand(zoperand)
          && !ZTyp.is_after_zoperand(zoperand) =>
      let move_then_perform = move_action =>
        switch (perform_operand(move_action, zoperand)) {
        | Failed
        | CursorEscaped(_) => assert(false)
        | Succeeded(zty) => perform(a, zty)
        };
      switch (side) {
      | Before => move_then_perform(MoveLeft)
      | After => move_then_perform(MoveRight)
      };

    | (Construct(SNum), CursorT(_, Hole)) =>
      Succeeded(ZT0(ZTyp.place_after_operand(Num)))
    | (Construct(SNum), CursorT(_)) => Failed

    | (Construct(SBool), CursorT(_, Hole)) =>
      Succeeded(ZT0(ZTyp.place_after_operand(Bool)))
    | (Construct(SBool), CursorT(_)) => Failed

    | (Construct(SList), CursorT(_)) =>
      Succeeded(ZT0(ListZ(ZT0(zoperand))))

    | (Construct(SParenthesized), CursorT(_)) =>
      Succeeded(ZT0(ParenthesizedZ(ZT0(zoperand))))

    | (Construct(SOp(os)), CursorT(_)) =>
      switch (operator_of_shape(os)) {
      | None => Failed
      | Some(op) =>
        Succeeded(ZT1(construct_operator(op, zoperand, (E, E))))
      }

    /* Zipper Cases */
    | (_, ParenthesizedZ(zbody)) =>
      switch (perform(a, zbody)) {
      | Failed => Failed
      | CursorEscaped(Before) => perform_operand(MoveLeft, zoperand)
      | CursorEscaped(After) => perform_operand(MoveRight, zoperand)
      | Succeeded(zbody) => Succeeded(ZT0(ParenthesizedZ(zbody)))
      }
    | (_, ListZ(zbody)) =>
      switch (perform(a, zbody)) {
      | Failed => Failed
      | CursorEscaped(Before) => perform_operand(MoveLeft, zoperand)
      | CursorEscaped(After) => perform_operand(MoveRight, zoperand)
      | Succeeded(zbody) => Succeeded(ZT0(ListZ(zbody)))
      }
    };
};

/*
 let abs_perform_Backspace_Before_op =
     (
       combine_for_Backspace_Space: ('e, 'z) => 'z,
       z_typecheck_fix_holes: (Contexts.t, MetaVarGen.t, 'z) => 'm,
       make_and_typecheck_OpSeqZ:
         (Contexts.t, MetaVarGen.t, 'z, Seq.opseq_surround('e, 'op)) => 'm,
       is_EmptyHole: 'e => bool,
       is_Space: 'op => bool,
       _Space: 'op,
       place_after: 'e => 'z,
       ctx: Contexts.t,
       u_gen: MetaVarGen.t,
       e0: 'e,
       ze0: 'z,
       surround: Seq.surround('e, 'op),
     )
     : option('m) =>
   switch (surround) {
   | EmptyPrefix(_) => None
   | EmptySuffix(prefix) =>
     switch (prefix) {
     | OperandPrefix(e1, op1) =>
       /* e1 op1 |ze0 */
       if (is_Space(op1)) {
         /* e1 |ze0 */
         let ze0' = combine_for_Backspace_Space(e1, ze0);
         Some(z_typecheck_fix_holes(ctx, u_gen, ze0'));
       } else {
         switch (is_EmptyHole(e1), is_EmptyHole(e0)) {
         | (true, true) =>
           /* _1 op1 |_0 --> _1| */
           let ze0' = place_after(e1);
           Some(z_typecheck_fix_holes(ctx, u_gen, ze0'));
         | (true, _) =>
           /* _1 op1 |e0 --> |e0 */
           Some(z_typecheck_fix_holes(ctx, u_gen, ze0))
         | (false, true) =>
           /* e1 op1 |_0 --> e1| */
           let ze0' = place_after(e1);
           Some(z_typecheck_fix_holes(ctx, u_gen, ze0'));
         | (false, false) =>
           /* e1 op1 |ze0 --> e1 |ze0 */
           let surround' = Seq.EmptySuffix(OperandPrefix(e1, _Space));
           Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
         };
       }
     | SeqPrefix(seq1, op1) =>
       /* seq1 op1 |ze0 */
       is_Space(op1)
         /* seq1 |ze0 */
         ? {
           let (e1, prefix') = OpSeqSurround.split_prefix_and_last(seq1);
           let surround' = Seq.EmptySuffix(prefix');
           let ze0' = combine_for_Backspace_Space(e1, ze0);
           Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0', surround'));
         }
         : {
           let (e1, prefix') = OpSeqSurround.split_prefix_and_last(seq1);
           if (is_EmptyHole(e0)) {
             /* prefix' e1 op1 |_0 --> prefix' e1| */
             let surround' = Seq.EmptySuffix(prefix');
             let ze0' = place_after(e1);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0', surround'));
           } else if (is_EmptyHole(e1)) {
             /* prefix' _1 op1 |e0 --> prefix' |e0 */
             let surround' = Seq.EmptySuffix(prefix');
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           } else {
             /* seq1 op1 |ze0 --> seq1 |ze0 */
             let prefix' = Seq.SeqPrefix(seq1, _Space);
             let surround' = Seq.EmptySuffix(prefix');
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           };
         }
     }
   | BothNonEmpty(prefix, suffix) =>
     switch (prefix) {
     | OperandPrefix(e1, op1) =>
       /* e1 op1 |ze0 ...suffix */
       is_Space(op1)
         /* e1 |ze0 ...suffix */
         ? {
           let ze0' = combine_for_Backspace_Space(e1, ze0);
           let surround' = Seq.EmptyPrefix(suffix);
           Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0', surround'));
         }
         : (
           if (is_EmptyHole(e0)) {
             /* e1 op1 |_0 suffix --> e1| suffix */
             let surround' = Seq.EmptyPrefix(suffix);
             let ze0' = place_after(e1);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0', surround'));
           } else if (is_EmptyHole(e1)) {
             /* _1 op1 |e0 suffix --> |e0 suffix */
             let surround' = Seq.EmptyPrefix(suffix);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           } else {
             /* e1 op1 |ze0 --> e1 |ze0 ...suffix */
             let surround' =
               Seq.BothNonEmpty(OperandPrefix(e1, _Space), suffix);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           }
         )
     | SeqPrefix(seq1, op1) =>
       /* seq1 op1 |ze0 ...suffix */
       is_Space(op1)
         /* seq1 |ze0 ...suffix */
         ? {
           let (e1, prefix') = OpSeqSurround.split_prefix_and_last(seq1);
           let ze0' = combine_for_Backspace_Space(e1, ze0);
           let surround' = Seq.BothNonEmpty(prefix', suffix);
           Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0', surround'));
         }
         : {
           let (e1, prefix') = OpSeqSurround.split_prefix_and_last(seq1);
           if (is_EmptyHole(e0)) {
             /* prefix' e1 op1 |_0 suffix --> prefix' e1| suffix */
             let surround' = Seq.BothNonEmpty(prefix', suffix);
             let ze0' = place_after(e1);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0', surround'));
           } else if (is_EmptyHole(e1)) {
             /* prefix' _1 op1 |e0 suffix --> prefix' |e0 suffix */
             let surround' = Seq.BothNonEmpty(prefix', suffix);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           } else {
             /* seq1 op1 |ze0 suffix --> seq1 |ze0 suffix */
             let prefix' = Seq.SeqPrefix(seq1, _Space);
             let surround' = Seq.BothNonEmpty(prefix', suffix);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           };
         }
     }
   };

 let abs_perform_Delete_After_op =
     (
       combine_for_Delete_Space: ('z, 'e) => 'z,
       z_typecheck_fix_holes: (Contexts.t, MetaVarGen.t, 'z) => 'm,
       make_and_typecheck_OpSeqZ:
         (Contexts.t, MetaVarGen.t, 'z, Seq.opseq_surround('e, 'op)) => 'm,
       is_EmptyHole: 'e => bool,
       is_Space: 'op => bool,
       _Space: 'op,
       place_before: 'e => 'z,
       ctx: Contexts.t,
       u_gen: MetaVarGen.t,
       e0: 'e,
       ze0: 'z,
       surround: Seq.opseq_surround('e, 'op),
     )
     : option('m) =>
   switch (surround) {
   | EmptySuffix(_) => None /* precluded by pattern begin match above */
   | EmptyPrefix(suffix) =>
     switch (suffix) {
     | OperandSuffix(op, e1) =>
       is_Space(op)
         ? {
           let ze0' = combine_for_Delete_Space(ze0, e1);
           Some(z_typecheck_fix_holes(ctx, u_gen, ze0'));
         }
         : (
           switch (is_EmptyHole(e0), is_EmptyHole(e1)) {
           | (true, true) =>
             /* _0| op _1 --> _0| */
             Some(z_typecheck_fix_holes(ctx, u_gen, ze0))
           | (true, false) =>
             /* _0| op e1 --> |e1 */
             let ze1 = place_before(e1);
             Some(z_typecheck_fix_holes(ctx, u_gen, ze1));
           | (false, true) =>
             /* e0| op _ --> e0| */
             Some(z_typecheck_fix_holes(ctx, u_gen, ze0))
           | (false, false) =>
             /* e0| op e1 --> e0| e1 */
             let surround' = Seq.EmptyPrefix(OperandSuffix(_Space, e1));
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           }
         )
     | SeqSuffix(op, seq) =>
       is_Space(op)
         ? {
           let (e, suffix') = OpSeqSurround.split_first_and_suffix(seq);
           let surround' = Seq.EmptyPrefix(suffix');
           let ze0' = combine_for_Delete_Space(ze0, e);
           Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0', surround'));
         }
         : {
           let (e1, suffix') = OpSeqSurround.split_first_and_suffix(seq);
           if (is_EmptyHole(e1)) {
             /* e0| op _ suffix' --> e0| suffix' */
             let surround' = Seq.EmptyPrefix(suffix');
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           } else if (is_EmptyHole(e0)) {
             /* _0| op e1 suffix' --> |e1 suffix' */
             let surround' = Seq.EmptyPrefix(suffix');
             let ze1 = place_before(e1);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze1, surround'));
           } else {
             /* e0| op seq --> e0| seq */
             let suffix' = Seq.SeqSuffix(_Space, seq);
             let surround' = Seq.EmptyPrefix(suffix');
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           };
         }
     }
   | BothNonEmpty(prefix, suffix) =>
     switch (suffix) {
     | OperandSuffix(op, e1) =>
       is_Space(op)
         ? {
           let ze0' = combine_for_Delete_Space(ze0, e1);
           let surround' = Seq.EmptySuffix(prefix);
           Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0', surround'));
         }
         : (
           if (is_EmptyHole(e1)) {
             /* prefix e0| op _ --> prefix e0| */
             let surround' = Seq.EmptySuffix(prefix);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           } else if (is_EmptyHole(e0)) {
             /* prefix _0| op e1 --> prefix |e1 */
             let surround' = Seq.EmptySuffix(prefix);
             let ze1 = place_before(e1);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze1, surround'));
           } else {
             /* prefix e0| op e1 --> e0| e1 */
             let surround' =
               Seq.BothNonEmpty(prefix, Seq.OperandSuffix(_Space, e1));
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           }
         )
     | SeqSuffix(op, seq) =>
       is_Space(op)
         ? {
           let (e, suffix') = OpSeqSurround.split_first_and_suffix(seq);
           let ze0' = combine_for_Delete_Space(ze0, e);
           let surround' = Seq.BothNonEmpty(prefix, suffix');
           Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0', surround'));
         }
         : {
           let (e1, suffix') = OpSeqSurround.split_first_and_suffix(seq);
           if (is_EmptyHole(e1)) {
             /* prefix e0| op _ suffix' --> prefix e0| suffix' */
             let surround' = Seq.BothNonEmpty(prefix, suffix');
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           } else if (is_EmptyHole(e0)) {
             /* prefix _0| op e1 suffix' --> prefix |e1 suffix' */
             let surround' = Seq.BothNonEmpty(prefix, suffix');
             let ze1 = place_before(e1);
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze1, surround'));
           } else {
             /* prefix e| op seq --> e| seq */
             let suffix' = Seq.SeqSuffix(_Space, seq);
             let surround' = Seq.BothNonEmpty(prefix, suffix');
             Some(make_and_typecheck_OpSeqZ(ctx, u_gen, ze0, surround'));
           };
         }
     }
   };

 let abs_perform_Construct_SOp_After =
     (
       bidelimit: 'e => 'e,
       new_EmptyHole: MetaVarGen.t => ('e, MetaVarGen.t),
       make_and_typecheck_OpSeq,
       make_and_typecheck_OpSeqZ:
         (Contexts.t, MetaVarGen.t, 'z, Seq.opseq_surround('e, 'op)) => 'm,
       is_Space: 'op => bool,
       place_before: 'e => 'z,
       ctx: Contexts.t,
       u_gen: MetaVarGen.t,
       e: 'e,
       op: 'op,
     )
     : 'm => {
   let e' = bidelimit(e);
   let (new_tm, u_gen) = new_EmptyHole(u_gen);
   if (is_Space(op)) {
     let prefix = Seq.OperandPrefix(e', op);
     let surround = Seq.EmptySuffix(prefix);
     make_and_typecheck_OpSeqZ(ctx, u_gen, place_before(new_tm), surround);
   } else {
     let new_seq = Seq.ExpOpExp(e', op, new_tm);
     make_and_typecheck_OpSeq(ctx, u_gen, OnDelim(1, After), new_seq);
   };
 };

 let abs_perform_Construct_SOp_Before =
     (
       bidelimit: 'e => 'e,
       new_EmptyHole: MetaVarGen.t => ('e, MetaVarGen.t),
       make_and_typecheck_OpSeq:
         (Contexts.t, MetaVarGen.t, CursorPosition.t, Seq.t('e, 'op)) => 'm,
       make_and_typecheck_OpSeqZ:
         (Contexts.t, MetaVarGen.t, 'z, Seq.opseq_surround('e, 'op)) => 'm,
       is_Space: 'op => bool,
       place_before: 'e => 'z,
       ctx: Contexts.t,
       u_gen: MetaVarGen.t,
       e: 'e,
       op: 'op,
     )
     : 'm => {
   let e' = bidelimit(e);
   let (new_tm, u_gen) = new_EmptyHole(u_gen);
   if (is_Space(op)) {
     let suffix = Seq.OperandSuffix(op, e');
     let surround = Seq.EmptyPrefix(suffix);
     make_and_typecheck_OpSeqZ(ctx, u_gen, place_before(new_tm), surround);
   } else {
     let new_seq = Seq.ExpOpExp(new_tm, op, e');
     make_and_typecheck_OpSeq(ctx, u_gen, OnDelim(1, After), new_seq);
   };
 };

 let abs_perform_Construct_SOp_After_surround =
     (
       new_EmptyHole: MetaVarGen.t => ('e, MetaVarGen.t),
       make_and_typecheck_OpSeq:
         (Contexts.t, MetaVarGen.t, CursorPosition.t, Seq.t('e, 'op)) => 'm,
       make_and_typecheck_OpSeqZ:
         (Contexts.t, MetaVarGen.t, 'z, Seq.opseq_surround('e, 'op)) => 'm,
       is_Space: 'op => bool,
       _Space: 'op,
       place_before: 'e => 'z,
       ctx: Contexts.t,
       u_gen: MetaVarGen.t,
       e: 'e,
       op: 'op,
       surround: Seq.opseq_surround('e, 'op),
     )
     : 'm =>
   switch (surround) {
   | EmptySuffix(prefix) =>
     let (new_tm, u_gen) = u_gen |> new_EmptyHole;
     if (is_Space(op)) {
       let prefix' = Seq.prefix_append_operand(prefix, e, op);
       let surround' = Seq.EmptySuffix(prefix');
       let ztm = place_before(new_tm);
       make_and_typecheck_OpSeqZ(ctx, u_gen, ztm, surround');
     } else {
       let new_seq =
         Seq.(SeqOpExp(t_of_prefix_and_last(prefix, e), op, new_tm));
       make_and_typecheck_OpSeq(
         ctx,
         u_gen,
         OnDelim(Seq.length(new_seq) - 1, After),
         new_seq,
       );
     };
   | EmptyPrefix(suffix) =>
     switch (suffix) {
     | Seq.OperandSuffix(op', e') =>
       is_Space(op)
         /* e| op' e' --> e |_ op' e' */
         ? {
           let prefix' = Seq.OperandPrefix(e, op);
           let suffix' = Seq.OperandSuffix(op', e');
           let surround' = Seq.BothNonEmpty(prefix', suffix');
           let (new_tm, u_gen) = new_EmptyHole(u_gen);
           make_and_typecheck_OpSeqZ(
             ctx,
             u_gen,
             place_before(new_tm),
             surround',
           );
         }
         : is_Space(op')
             /* e| e' --> e op| e' */
             ? {
               let new_seq = Seq.ExpOpExp(e, op, e');
               make_and_typecheck_OpSeq(
                 ctx,
                 u_gen,
                 OnDelim(1, After),
                 new_seq,
               );
             }
             /* e| op' e' --> e op| _ op' e' */
             : {
               let (new_tm, u_gen) = u_gen |> new_EmptyHole;
               let new_seq = Seq.SeqOpExp(ExpOpExp(e, op, new_tm), op', e');
               make_and_typecheck_OpSeq(
                 ctx,
                 u_gen,
                 OnDelim(1, After),
                 new_seq,
               );
             }
     | SeqSuffix(op', seq') =>
       is_Space(op)
         /* e| op' seq' --> e |_ op' seq' */
         ? {
           let prefix' = Seq.OperandPrefix(e, op);
           let surround' = Seq.BothNonEmpty(prefix', suffix);
           let (new_tm, u_gen) = new_EmptyHole(u_gen);
           make_and_typecheck_OpSeqZ(
             ctx,
             u_gen,
             place_before(new_tm),
             surround',
           );
         }
         : is_Space(op')
             /* e| seq' --> e op| seq' */
             ? {
               let new_seq = Seq.operand_op_seq(e, op, seq');
               make_and_typecheck_OpSeq(
                 ctx,
                 u_gen,
                 OnDelim(1, After),
                 new_seq,
               );
             }
             /* e| op' seq' --> e op| _ op' seq' */
             : {
               let (new_tm, u_gen) = u_gen |> new_EmptyHole;
               let new_seq =
                 Seq.operand_op_seq(
                   e,
                   op,
                   Seq.operand_op_seq(new_tm, op', seq'),
                 );
               make_and_typecheck_OpSeq(
                 ctx,
                 u_gen,
                 OnDelim(1, After),
                 new_seq,
               );
             }
     }
   | BothNonEmpty(prefix, suffix) =>
     switch (suffix) {
     | OperandSuffix(op', e') =>
       is_Space(op)
         /* prefix e| op' e' --> prefix e |_ op' e' */
         ? {
           let prefix' = Seq.prefix_append_operand(prefix, e, op);
           let suffix' = Seq.OperandSuffix(op', e');
           let surround' = Seq.BothNonEmpty(prefix', suffix');
           let (new_tm, u_gen) = new_EmptyHole(u_gen);
           make_and_typecheck_OpSeqZ(
             ctx,
             u_gen,
             place_before(new_tm),
             surround',
           );
         }
         : is_Space(op')
             /* prefix e| e' --> prefix e op| e' */
             ? {
               let new_seq =
                 Seq.(SeqOpExp(t_of_prefix_and_last(prefix, e), op, e'));
               let k = Seq.prefix_length(prefix) + 1;
               make_and_typecheck_OpSeq(
                 ctx,
                 u_gen,
                 OnDelim(k, After),
                 new_seq,
               );
             }
             /* prefix e| op' e' --> prefix e op| _ op' e' */
             : {
               let (new_tm, u_gen) = u_gen |> new_EmptyHole;
               let new_seq =
                 Seq.(
                   SeqOpExp(
                     SeqOpExp(t_of_prefix_and_last(prefix, e), op, new_tm),
                     op',
                     e',
                   )
                 );
               let k = Seq.prefix_length(prefix) + 1;
               make_and_typecheck_OpSeq(
                 ctx,
                 u_gen,
                 OnDelim(k, After),
                 new_seq,
               );
             }
     | SeqSuffix(op', seq') =>
       is_Space(op)
         /* prefix e| op' seq' --> prefix e |_ op' seq' */
         ? {
           let prefix' = Seq.prefix_append_operand(prefix, e, op);
           let surround' = Seq.BothNonEmpty(prefix', suffix);
           let (new_tm, u_gen) = new_EmptyHole(u_gen);
           make_and_typecheck_OpSeqZ(
             ctx,
             u_gen,
             place_before(new_tm),
             surround',
           );
         }
         : is_Space(op')
             /* prefix e| seq' --> prefix e op| seq' */
             ? {
               let new_seq =
                 Seq.(seq_op_seq(t_of_prefix_and_last(prefix, e), op, seq'));
               let k = Seq.prefix_length(prefix) + 1;
               make_and_typecheck_OpSeq(
                 ctx,
                 u_gen,
                 OnDelim(k, After),
                 new_seq,
               );
             }
             /* prefix e| op' seq' --> prefix e op| _ op' seq' */
             : {
               let (new_tm, u_gen) = u_gen |> new_EmptyHole;
               let new_seq =
                 Seq.(
                   seq_op_seq(
                     SeqOpExp(t_of_prefix_and_last(prefix, e), op, new_tm),
                     op',
                     seq',
                   )
                 );
               let k = Seq.prefix_length(prefix) + 1;
               make_and_typecheck_OpSeq(
                 ctx,
                 u_gen,
                 OnDelim(k, After),
                 new_seq,
               );
             }
     }
   };

 let abs_perform_Construct_SOp_Before_surround =
     (
       new_EmptyHole: MetaVarGen.t => ('e, MetaVarGen.t),
       make_and_typecheck_OpSeq:
         (Contexts.t, MetaVarGen.t, CursorPosition.t, Seq.t('e, 'op)) => 'm,
       make_and_typecheck_OpSeqZ:
         (Contexts.t, MetaVarGen.t, 'z, Seq.opseq_surround('e, 'op)) => 'm,
       is_Space: 'op => bool,
       _Space: 'op,
       place_before: 'e => 'z,
       ctx: Contexts.t,
       u_gen: MetaVarGen.t,
       e0: 'e,
       op: 'op,
       surround: Seq.opseq_surround('e, 'op),
     )
     : 'm =>
   switch (surround) {
   | EmptyPrefix(suffix) =>
     /* |e0 ... --> |_ op e0 ... */
     let suffix' = Seq.suffix_prepend_exp(suffix, op, e0);
     let surround' = Seq.EmptyPrefix(suffix');
     let (new_tm, u_gen) = new_EmptyHole(u_gen);
     make_and_typecheck_OpSeqZ(ctx, u_gen, place_before(new_tm), surround');
   | EmptySuffix(OperandPrefix(e1, op') as prefix) =>
     is_Space(op')
       ? is_Space(op)
           /* e1 |e0 --> e1 |_ e0 */
           ? {
             let suffix' = Seq.OperandSuffix(_Space, e0);
             let surround' = Seq.BothNonEmpty(prefix, suffix');
             let (new_tm, u_gen) = new_EmptyHole(u_gen);
             make_and_typecheck_OpSeqZ(
               ctx,
               u_gen,
               place_before(new_tm),
               surround',
             );
           }
           /* e1 |e0 --> e1 op| e0 */
           : {
             let new_seq = Seq.ExpOpExp(e1, op, e0);
             make_and_typecheck_OpSeq(ctx, u_gen, OnDelim(1, After), new_seq);
           }
       /* e1 op' |e0 --> e1 op' _ op| e0 */
       : {
         let (new_tm, u_gen) = new_EmptyHole(u_gen);
         let new_seq = Seq.(SeqOpExp(ExpOpExp(e1, op', new_tm), op, e0));
         make_and_typecheck_OpSeq(ctx, u_gen, OnDelim(2, After), new_seq);
       }
   | EmptySuffix(SeqPrefix(seq1, op') as prefix) =>
     is_Space(op')
       ? is_Space(op)
           /* seq1 |e0 --> seq1 |_ e0 */
           ? {
             let suffix' = Seq.OperandSuffix(_Space, e0);
             let surround' = Seq.BothNonEmpty(prefix, suffix');
             let (new_tm, u_gen) = new_EmptyHole(u_gen);
             make_and_typecheck_OpSeqZ(
               ctx,
               u_gen,
               place_before(new_tm),
               surround',
             );
           }
           /* seq1 |e0 --> seq1 op| e0 */
           : {
             let new_seq = Seq.SeqOpExp(seq1, op, e0);
             let k = Seq.length(seq1);
             make_and_typecheck_OpSeq(ctx, u_gen, OnDelim(k, After), new_seq);
           }
       /* seq1 op' |e0 --> seq1 op' _ op| e0 */
       : {
         let (new_tm, u_gen) = new_EmptyHole(u_gen);
         let new_seq = Seq.(SeqOpExp(SeqOpExp(seq1, op', new_tm), op, e0));
         let k = Seq.length(seq1) + 1;
         make_and_typecheck_OpSeq(ctx, u_gen, OnDelim(k, After), new_seq);
       }
   | BothNonEmpty(OperandPrefix(e1, op') as prefix, suffix) =>
     is_Space(op')
       ? is_Space(op)
           /* e1 |e0 suffix --> e1 |_ e0 suffix */
           ? {
             let suffix' = Seq.suffix_prepend_exp(suffix, _Space, e0);
             let surround' = Seq.BothNonEmpty(prefix, suffix');
             let (new_tm, u_gen) = new_EmptyHole(u_gen);
             make_and_typecheck_OpSeqZ(
               ctx,
               u_gen,
               place_before(new_tm),
               surround',
             );
           }
           /* e1 |e0 suffix --> e1 op| e0 suffix */
           : {
             let new_seq =
               Seq.(t_of_seq_and_suffix(ExpOpExp(e1, op, e0), suffix));
             make_and_typecheck_OpSeq(ctx, u_gen, OnDelim(1, After), new_seq);
           }
       /* e1 op' |e0 suffix --> e1 op' _ op| e0 suffix */
       : {
         let (new_tm, u_gen) = new_EmptyHole(u_gen);
         let new_seq =
           Seq.(
             t_of_seq_and_suffix(
               SeqOpExp(ExpOpExp(e1, op', new_tm), op, e0),
               suffix,
             )
           );
         make_and_typecheck_OpSeq(ctx, u_gen, OnDelim(2, After), new_seq);
       }
   | BothNonEmpty(SeqPrefix(seq1, op') as prefix, suffix) =>
     is_Space(op')
       ? is_Space(op)
           /* seq1 |e0 suffix --> seq1 |_ e0 suffix */
           ? {
             let suffix' = Seq.suffix_prepend_exp(suffix, _Space, e0);
             let surround' = Seq.BothNonEmpty(prefix, suffix');
             let (new_tm, u_gen) = new_EmptyHole(u_gen);
             make_and_typecheck_OpSeqZ(
               ctx,
               u_gen,
               place_before(new_tm),
               surround',
             );
           }
           /* seq1 |e0 suffix --> seq1 op| e0 suffix */
           : {
             let new_seq =
               Seq.(t_of_seq_and_suffix(SeqOpExp(seq1, op, e0), suffix));
             let k = Seq.length(seq1);
             make_and_typecheck_OpSeq(ctx, u_gen, OnDelim(k, After), new_seq);
           }
       /* seq1 op' |e0 suffix --> seq op' _ op| e0 suffix */
       : {
         let (new_tm, u_gen) = new_EmptyHole(u_gen);
         let new_seq =
           Seq.(
             t_of_seq_and_suffix(
               SeqOpExp(SeqOpExp(seq1, op', new_tm), op, e0),
               suffix,
             )
           );
         let k = Seq.length(seq1) + 1;
         make_and_typecheck_OpSeq(ctx, u_gen, OnDelim(k, After), new_seq);
       }
   };
 */

let check_valid = (x: Var.t, result: result('a)): result('a) =>
  if (Var.is_valid(x)) {
    result;
  } else {
    Failed;
  };

let _construct_operator_after_zoperand =
    (
      ~is_Space: 'operator => bool,
      ~new_EmptyHole: MetaVarGen.t => ('operand, MetaVarGen.t),
      ~erase_zoperand: 'zoperand => 'operand,
      ~place_before_operand: 'operand => 'zoperand,
      ~place_after_operator: 'operator => option('zoperator),
      u_gen: MetaVarGen.t,
      operator: 'operator,
      zoperand: 'zoperand,
      (prefix, suffix): Seq.operand_surround('operand, 'operator),
    )
    : (ZSeq.t('operand, 'operator, 'zoperand, 'zoperator), MetaVarGen.t) => {
  let operand = zoperand |> erase_zoperand;
  switch (operator |> place_after_operator) {
  | None =>
    // operator == Space
    // ... + [k]| + [k+1] + ...   ==>   ... + [k]  |_ + [k+1] + ...
    let (hole, u_gen) = u_gen |> new_EmptyHole;
    let new_prefix = Seq.A(operator, S(operand, prefix));
    let new_zoperand = hole |> place_before_operand;
    (ZOperand(new_zoperand, (new_prefix, suffix)), u_gen);
  | Some(zoperator) =>
    let new_prefix = Seq.S(operand, prefix);
    let (new_suffix, u_gen) =
      switch (suffix) {
      | A(op, new_suffix) when op |> is_Space =>
        // zoperator overwrites Space
        // ... + [k]|  [k+1] + ...   ==>   ... + [k] *| [k+1] + ...
        (new_suffix, u_gen)
      | _ =>
        // ... + [k]| + [k+1] + ...   ==>   ... + [k] *| _ + [k+1] + ...
        let (hole, u_gen) = u_gen |> new_EmptyHole;
        (Seq.S(hole, suffix), u_gen);
      };
    (ZOperator(zoperator, (new_prefix, new_suffix)), u_gen);
  };
};
let _construct_operator_before_zoperand =
    (
      ~is_Space: 'operator => bool,
      ~new_EmptyHole: MetaVarGen.t => ('operand, MetaVarGen.t),
      ~erase_zoperand: 'zoperand => 'operand,
      ~place_before_operand: 'operand => 'zoperand,
      ~place_after_operator: 'operator => option('zoperator),
      u_gen: MetaVarGen.t,
      operator: 'operator,
      zoperand: 'zoperand,
      (prefix, suffix): Seq.operand_surround('operand, 'operator),
    )
    : (ZSeq.t('operand, 'operator, 'zoperand, 'zoperator), MetaVarGen.t) => {
  // symmetric to construct_operator_after_zoperand
  let mirror_surround = (suffix, prefix);
  let (mirror_zseq, u_gen) =
    _construct_operator_after_zoperand(
      ~is_Space,
      ~new_EmptyHole,
      ~erase_zoperand,
      ~place_before_operand,
      ~place_after_operator,
      u_gen,
      operator,
      zoperand,
      mirror_surround,
    );
  let zseq: ZSeq.t(_) =
    switch (mirror_zseq) {
    | ZOperator(z, (suffix, prefix)) => ZOperator(z, (prefix, suffix))
    | ZOperand(z, (suffix, prefix)) => ZOperand(z, (prefix, suffix))
    };
  (zseq, u_gen);
};

let _delete_operator =
    (
      ~is_EmptyHole: 'operand => bool,
      ~place_before_operand: 'operand => 'zoperand,
      ~place_after_operand: 'operand => 'zoperand,
      ~place_after_operator: 'operator => option('zoperator),
      (prefix, suffix): Seq.operator_surround('operand, 'operator),
    )
    : ZSeq.t('operand, 'operator, 'zoperand, 'zoperator) =>
  switch (prefix, suffix) {
  /* _ +<| [1] + ...   ==>   |[1] + ... */
  | (S(operand, E as prefix), S(suffix_hd, new_suffix))
      when operand |> is_EmptyHole =>
    let zoperand = suffix_hd |> place_before_operand;
    ZOperand(zoperand, (prefix, new_suffix));

  | (S(operand, A(operator, prefix_tl) as prefix), suffix)
      when operand |> is_EmptyHole =>
    switch (operator |> place_after_operator) {
    /* ... + [k-2]  _ +<| [k] + ...   ==>  ... + [k-2] |[k] + ... */
    | None =>
      let S(suffix_hd, new_suffix) = suffix;
      let zoperand = suffix_hd |> place_before_operand;
      ZOperand(zoperand, (prefix, new_suffix));
    /* ... + [k-2] + _ +<| [k] + ...   ==>   ... + [k-2] +| [k] + ... */
    | Some(zoperator) =>
      let new_prefix = prefix_tl;
      ZOperator(zoperator, (new_prefix, suffix));
    }

  /* ... + [k-1] +<|  _ + ...   ==>   ... + [k-1]| + ... */
  | (S(prefix_hd, new_prefix), S(operand, new_suffix))
      when operand |> is_EmptyHole =>
    let zoperand = prefix_hd |> place_after_operand;
    ZOperand(zoperand, (new_prefix, new_suffix));

  /* ... + [k-1] +<| [k] + ...   ==>   ... + [k-1]| [k] + ... */
  | (S(prefix_hd, new_prefix), _) =>
    let zoperand = prefix_hd |> place_after_operand;
    let new_suffix = Seq.A(UHPat.Space, suffix);
    ZOperand(zoperand, (new_prefix, new_suffix));
  };

module Pat = {
  let operator_of_shape: operator_shape => option(UHPat.operator) =
    fun
    | SComma => Some(Comma)
    | SSpace => Some(Space)
    | SCons => Some(Cons)
    | SAnd
    | SOr
    | SMinus
    | SPlus
    | STimes
    | SLessThan
    | SGreaterThan
    | SEquals
    | SArrow
    | SVBar => None;

  let shape_of_operator: UHPat.operator => operator_shape =
    fun
    | Comma => SComma
    | Space => SSpace
    | Cons => SCons;

  let mk_ZOpSeq =
    ZOpSeq.mk(
      ~associate=Associator.associate_pat,
      ~erase_zoperand=ZPat.erase_zoperand,
      ~erase_zoperator=ZPat.erase_zoperator,
    );

  let mk_and_syn_fix_ZOpSeq =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, zseq: ZPat.zseq)
      : (ZPat.t, HTyp.t, Contexts.t, MetaVarGen.t) => {
    let zopseq = mk_ZOpSeq(zseq);
    Statics.Pat.syn_fix_holes_z(ctx, u_gen, ZP1(zopseq));
  };
  let mk_and_ana_fix_ZOpSeq =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, zseq: ZPat.zseq, ty: HTyp.t)
      : (ZPat.t, Contexts.t, MetaVarGen.t) => {
    let zopseq = mk_ZOpSeq(zseq);
    Statics.Pat.ana_fix_holes_z(ctx, u_gen, ZP1(zopseq), ty);
  };

  let mk_syn_result = (ctx: Contexts.t, u_gen: MetaVarGen.t, zp: ZPat.t) =>
    switch (Statics.Pat.syn(ctx, zp |> ZPat.erase)) {
    | None => Failed
    | Some((ty, ctx)) => Succeeded((zp, ty, ctx, u_gen))
    };
  let mk_ana_result =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, zp: ZPat.t, ty: HTyp.t) =>
    switch (Statics.Pat.ana(ctx, zp |> ZPat.erase, ty)) {
    | None => Failed
    | Some(ctx) => Succeeded((zp, ctx, u_gen))
    };

  let syn_cursor_escaped_zopseq = (ctx: Contexts.t, u_gen: MetaVarGen.t) =>
    _cursor_escaped_zopseq(
      ~move_cursor_left=ZPat.move_cursor_left_zopseq,
      ~move_cursor_right=ZPat.move_cursor_right_zopseq,
      ~mk_result=zopseq =>
      mk_syn_result(ctx, u_gen, ZP1(zopseq))
    );
  let ana_cursor_escaped_zopseq =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, ty: HTyp.t) =>
    _cursor_escaped_zopseq(
      ~move_cursor_left=ZPat.move_cursor_left_zopseq,
      ~move_cursor_right=ZPat.move_cursor_right_zopseq,
      ~mk_result=zopseq =>
      mk_ana_result(ctx, u_gen, ZP1(zopseq), ty)
    );

  let construct_operator_before_zoperand =
    _construct_operator_before_zoperand(
      ~is_Space=UHPat.is_Space,
      ~new_EmptyHole=UHPat.new_EmptyHole,
      ~erase_zoperand=ZPat.erase_zoperand,
      ~place_before_operand=ZPat.place_before_operand,
      ~place_after_operator=ZPat.place_after_operator,
    );
  let construct_operator_after_zoperand =
    _construct_operator_after_zoperand(
      ~is_Space=UHPat.is_Space,
      ~new_EmptyHole=UHPat.new_EmptyHole,
      ~erase_zoperand=ZPat.erase_zoperand,
      ~place_before_operand=ZPat.place_before_operand,
      ~place_after_operator=ZPat.place_after_operator,
    );

  let delete_operator =
    _delete_operator(
      ~is_EmptyHole=UHPat.is_EmptyHole,
      ~place_before_operand=ZPat.place_before_operand,
      ~place_after_operand=ZPat.place_after_operand,
      ~place_after_operator=ZPat.place_after_operator,
    );

  let resurround =
      (zp: ZPat.t, (prefix, suffix) as surround: ZPat.operand_surround)
      : ZPat.zseq =>
    switch (zp) {
    | ZP0(zoperand) => ZOperand(zoperand, surround)
    | ZP1(ZOpSeq(_, ZOperand(zoperand, (inner_prefix, inner_suffix)))) =>
      let new_prefix = Seq.affix_affix(inner_prefix, prefix);
      let new_suffix = Seq.affix_affix(inner_suffix, suffix);
      ZOperand(zoperand, (new_prefix, new_suffix));
    | ZP1(ZOpSeq(_, ZOperator(zoperator, (inner_prefix, inner_suffix)))) =>
      let new_prefix = Seq.seq_affix(inner_prefix, prefix);
      let new_suffix = Seq.seq_affix(inner_suffix, suffix);
      ZOperator(zoperator, (new_prefix, new_suffix));
    };

  let rec syn_perform =
          (ctx: Contexts.t, u_gen: MetaVarGen.t, a: t, zp: ZPat.t)
          : result((ZPat.t, HTyp.t, Contexts.t, MetaVarGen.t)) =>
    switch (zp) {
    | ZP1(zp1) => syn_perform_opseq(ctx, u_gen, a, zp1)
    | ZP0(zp0) => syn_perform_operand(ctx, u_gen, a, zp0)
    }
  and syn_perform_opseq =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        a: t,
        ZOpSeq(skel, zseq) as zopseq: ZPat.zopseq,
      )
      : result((ZPat.t, HTyp.t, Contexts.t, MetaVarGen.t)) =>
    switch (a, zseq) {
    /* Invalid cursor positions */
    | (_, ZOperator((OnText(_) | OnDelim(_), _), _)) => Failed

    /* Invalid actions */
    | (UpdateApPalette(_), ZOperator(_)) => Failed

    /* Movement */
    | (MoveTo(path), _) =>
      switch (CursorPath.Pat.follow(path, P1(zopseq |> ZPat.erase_zopseq))) {
      | None => Failed
      | Some(zp) => mk_syn_result(ctx, u_gen, zp)
      }
    | (MoveToBefore(steps), _) =>
      switch (
        CursorPath.Pat.follow_steps(
          ~side=Before,
          steps,
          P1(zopseq |> ZPat.erase_zopseq),
        )
      ) {
      | None => Failed
      | Some(zp) => mk_syn_result(ctx, u_gen, zp)
      }
    | (MoveToPrevHole, _) =>
      switch (CursorPath.(prev_hole_steps(Pat.holes_zopseq(zopseq, [])))) {
      | None => Failed
      | Some(steps) =>
        syn_perform_opseq(ctx, u_gen, MoveToBefore(steps), zopseq)
      }
    | (MoveToNextHole, _) =>
      switch (CursorPath.(next_hole_steps(Pat.holes_zopseq(zopseq, [])))) {
      | None => Failed
      | Some(steps) =>
        syn_perform_opseq(ctx, u_gen, MoveToBefore(steps), zopseq)
      }
    | (MoveLeft, _) =>
      switch (ZPat.(ZP1(zopseq) |> move_cursor_left)) {
      | None => CursorEscaped(Before)
      | Some(zp) => mk_syn_result(ctx, u_gen, zp)
      }
    | (MoveRight, _) =>
      switch (ZPat.(ZP1(zopseq) |> move_cursor_right)) {
      | None => CursorEscaped(After)
      | Some(zp) => mk_syn_result(ctx, u_gen, zp)
      }

    /* Deletion */

    | (Delete, ZOperator((OnOp(After), _), _)) =>
      syn_cursor_escaped_zopseq(ctx, u_gen, After, zopseq)
    | (Backspace, ZOperator((OnOp(Before), _), _)) =>
      syn_cursor_escaped_zopseq(ctx, u_gen, Before, zopseq)

    /* Delete before operator == Backspace after operator */
    | (Delete, ZOperator((OnOp(Before), op), surround)) =>
      syn_perform_opseq(
        ctx,
        u_gen,
        Backspace,
        ZOpSeq(skel, ZOperator((OnOp(After), op), surround)),
      )

    /* ... + [k-1] +<| [k] + ... */
    | (Backspace, ZOperator((OnOp(After), _), surround)) =>
      let new_zseq = delete_operator(surround);
      Succeeded(mk_and_syn_fix_ZOpSeq(ctx, u_gen, new_zseq));

    /* ... + [k-1]  <|_ + [k+1] + ...  ==>   ... + [k-1]| + [k+1] + ... */
    | (
        Backspace,
        ZOperand(
          CursorP(_, EmptyHole(_)) as zhole,
          (A(Space, prefix_tl), suffix),
        ),
      )
        when ZPat.is_before_zoperand(zhole) =>
      let S(operand, new_prefix) = prefix_tl;
      let zoperand = operand |> ZPat.place_after_operand;
      let new_zseq = ZSeq.ZOperand(zoperand, (new_prefix, suffix));
      Succeeded(mk_and_syn_fix_ZOpSeq(ctx, u_gen, new_zseq));

    /* ... + [k-1] + _|>  [k+1] + ...  ==>   ... + [k-1] + |[k+1] + ... */
    | (
        Delete,
        ZOperand(
          CursorP(_, EmptyHole(_)) as zhole,
          (prefix, A(Space, suffix_tl)),
        ),
      )
        when ZPat.is_after_zoperand(zhole) =>
      let S(operand, new_suffix) = suffix_tl;
      let zoperand = operand |> ZPat.place_before_operand;
      let new_zseq = ZSeq.ZOperand(zoperand, (prefix, new_suffix));
      Succeeded(mk_and_syn_fix_ZOpSeq(ctx, u_gen, new_zseq));

    /* Construction */

    /* construction on operators either becomes movement... */
    | (Construct(SOp(SSpace)), ZOperator(zoperator, _))
        when ZPat.is_after_zoperator(zoperator) =>
      syn_perform_opseq(ctx, u_gen, MoveRight, zopseq)
    /* ...or construction after movement */
    | (Construct(_) as a, ZOperator(zoperator, _)) =>
      let move_cursor =
        ZPat.is_before_zoperator(zoperator)
          ? ZPat.move_cursor_left_zopseq : ZPat.move_cursor_right_zopseq;
      switch (zopseq |> move_cursor) {
      | None => Failed
      | Some(zopseq) => syn_perform_opseq(ctx, u_gen, a, zopseq)
      };

    | (Construct(SOp(os)), ZOperand(zoperand, surround))
        when
          ZPat.is_before_zoperand(zoperand)
          || ZPat.is_after_zoperand(zoperand) =>
      switch (operator_of_shape(os)) {
      | None => Failed
      | Some(operator) =>
        let construct_operator =
          ZPat.is_before_zoperand(zoperand)
            ? construct_operator_before_zoperand
            : construct_operator_after_zoperand;
        let (zseq, u_gen) =
          construct_operator(u_gen, operator, zoperand, surround);
        Succeeded(mk_and_syn_fix_ZOpSeq(ctx, u_gen, zseq));
      }

    /* Zipper */
    | (_, ZOperand(zoperand, surround)) =>
      switch (CursorInfo.Pat.syn_cursor_info_zopseq(ctx, zopseq)) {
      | None => Failed
      | Some(ci) =>
        switch (ci |> CursorInfo.type_mode) {
        | None => Failed
        | Some(Syn) =>
          switch (syn_perform_operand(ctx, u_gen, a, zoperand)) {
          | Failed => Failed
          | CursorEscaped(side) =>
            syn_cursor_escaped_zopseq(ctx, u_gen, side, zopseq)
          | Succeeded((zp, _, _, u_gen)) =>
            let zseq = resurround(zp, surround);
            Succeeded(mk_and_syn_fix_ZOpSeq(ctx, u_gen, zseq));
          }
        | Some(Ana(ty_zoperand)) =>
          switch (ana_perform_operand(ctx, u_gen, a, zoperand, ty_zoperand)) {
          | Failed => Failed
          | CursorEscaped(side) =>
            syn_cursor_escaped_zopseq(ctx, u_gen, side, zopseq)
          | Succeeded((zp, _, u_gen)) =>
            let new_zseq = resurround(zp, surround);
            Succeeded(mk_and_syn_fix_ZOpSeq(ctx, u_gen, zseq));
          }
        }
      }
    }
  and syn_perform_operand =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, a: t, zoperand: ZPat.zoperand)
      : result((ZPat.t, HTyp.t, Contexts.t, MetaVarGen.t)) => {
    switch (a, zoperand) {
    /* Invalid cursor positions */
    | (
        _,
        CursorP(
          OnText(_),
          EmptyHole(_) | Wild(_) | ListNil(_) | Parenthesized(_) | Inj(_),
        ) |
        CursorP(OnDelim(_), Var(_) | NumLit(_) | BoolLit(_)) |
        CursorP(OnOp(_), _),
      ) =>
      Failed
    | (_, CursorP(cursor, operand))
        when !ZPat.is_valid_cursor_operand(cursor, operand) =>
      Failed

    /* Invalid actions */
    | (
        Construct(
          SApPalette(_) | SNum | SBool | SList | SAsc | SLet | SLine | SLam |
          SCase,
        ) |
        UpdateApPalette(_),
        _,
      ) =>
      Failed

    /* Movement */
    /* NOTE: we don't need to handle movement actions here for the purposes of the UI,
     * since it's handled at the top (expression) level, but for the sake of API completeness
     * we include it */
    | (MoveTo(path), _) =>
      let operand = zoperand |> ZPat.erase_zoperand;
      switch (CursorPath.Pat.follow(path, P0(operand))) {
      | None => Failed
      | Some(zp) => mk_syn_result(ctx, u_gen, zp)
      };
    | (MoveToBefore(steps), _) =>
      let operand = zoperand |> ZPat.erase_zoperand;
      switch (CursorPath.Pat.follow_steps(steps, P0(operand))) {
      | None => Failed
      | Some(zp) => mk_syn_result(ctx, u_gen, zp)
      };
    | (MoveToPrevHole, _) =>
      switch (CursorPath.(prev_hole_steps(Pat.holes_zoperand(zoperand, [])))) {
      | None => Failed
      | Some(steps) =>
        syn_perform_operand(ctx, u_gen, MoveToBefore(steps), zoperand)
      }
    | (MoveToNextHole, _) =>
      switch (CursorPath.(next_hole_steps(Pat.holes_zoperand(zoperand, [])))) {
      | None => Failed
      | Some(steps) =>
        syn_perform_operand(ctx, u_gen, MoveToBefore(steps), zoperand)
      }
    | (MoveLeft, _) =>
      switch (ZPat.(ZP0(zoperand) |> move_cursor_left)) {
      | None => CursorEscaped(Before)
      | Some(zp) => mk_syn_result(ctx, u_gen, zp)
      }
    | (MoveRight, _) =>
      switch (ZPat.(ZP0(zoperand) |> move_cursor_right)) {
      | None => CursorEscaped(After)
      | Some(zp) => mk_syn_result(ctx, u_gen, zp)
      }

    /* Backspace and Delete */

    | (Backspace, _) when ZPat.is_before_zoperand(zoperand) =>
      CursorEscaped(Before)
    | (Delete, _) when ZPat.is_after_zoperand(zoperand) =>
      CursorEscaped(After)

    /* ( _ <|)   ==>   ( _| ) */
    | (Backspace, CursorP(OnDelim(_, Before), _)) =>
      zoperand |> ZPat.is_before_zoperand
        ? CursorEscaped(Before)
        : syn_perform_operand(ctx, u_gen, MoveLeft, zoperand)
    /* (|> _ )   ==>   ( |_ ) */
    | (Delete, CursorP(OnDelim(_, After), _)) =>
      zoperand |> ZPat.is_after_zoperand
        ? CursorEscaped(After)
        : syn_perform_operand(ctx, u_gen, MoveRight, zoperand)

    /* Delete before delimiter == Backspace after delimiter */
    | (Delete, CursorP(OnDelim(k, Before), operand)) =>
      syn_perform_operand(
        ctx,
        u_gen,
        Backspace,
        CursorP(OnDelim(k, After), operand),
      )

    | (Backspace, CursorP(_, EmptyHole(_) as operand)) =>
      if (ZPat.is_after_zoperand(zoperand)) {
        let zp = ZPat.(ZP0(place_before_operand(operand)));
        Succeeded((zp, Hole, ctx, u_gen));
      } else {
        CursorEscaped(Before);
      }
    | (Delete, CursorP(_, EmptyHole(_) as operand)) =>
      if (ZPat.is_before_zoperand(zoperand)) {
        let zp = ZPat.(ZP0(place_after_operand(operand)));
        Succeeded((zp, Hole, ctx, u_gen));
      } else {
        CursorEscaped(After);
      }

    | (
        Backspace | Delete,
        CursorP(
          OnText(_) | OnDelim(_, _),
          Var(_, _, _) | Wild(_) | NumLit(_, _) | BoolLit(_, _) | ListNil(_),
        ),
      ) =>
      let (zhole, u_gen) = ZPat.new_EmptyHole(u_gen);
      let zp = ZPat.ZP0(zhole);
      Succeeded((zp, Hole, ctx, u_gen));

    /* ( _ )<|  ==>  _| */
    /* (<| _ )  ==>  |_ */
    | (
        Backspace,
        CursorP(OnDelim(k, After), Parenthesized(body) | Inj(_, _, body)),
      ) =>
      let place_cursor = k == 0 ? ZPat.place_before : ZPat.place_after;
      Succeeded(
        Statics.Pat.syn_fix_holes_z(ctx, u_gen, body |> place_cursor),
      );

    /* Construction */

    | (Construct(SOp(SSpace)), CursorP(OnDelim(_, After), _)) =>
      syn_perform_operand(ctx, u_gen, MoveRight, zoperand)
    | (Construct(_) as a, CursorP(OnDelim(_, side), _))
        when
          !ZPat.is_before_zoperand(zoperand)
          && !ZPat.is_after_zoperand(zoperand) =>
      let move_then_perform = move_action =>
        switch (syn_perform_operand(ctx, u_gen, move_action, zoperand)) {
        | Failed
        | CursorEscaped(_) => assert(false)
        | Succeeded((zp, _, _, u_gen)) => syn_perform(ctx, u_gen, a, zp)
        };
      switch (side) {
      | Before => move_then_perform(MoveLeft)
      | After => move_then_perform(MoveRight)
      };

    | (
        Construct(SVar(x, cursor)),
        CursorP(_, EmptyHole(_) | Wild(_) | Var(_) | NumLit(_) | BoolLit(_)),
      ) =>
      if (Var.is_true(x)) {
        let zp = ZPat.(ZP0(CursorP(cursor, BoolLit(NotInHole, true))));
        Succeeded((zp, Bool, ctx, u_gen));
      } else if (Var.is_false(x)) {
        let zp = ZPat.(ZP0(CursorP(cursor, BoolLit(NotInHole, false))));
        Succeeded((zp, Bool, ctx, u_gen));
      } else if (Var.is_let(x)) {
        let (u, u_gen) = MetaVarGen.next(u_gen);
        let var = UHPat.Var(NotInHole, InVarHole(Keyword(Let), u), x);
        let zp = ZPat.(ZP0(CursorP(cursor, var)));
        Succeeded((zp, Hole, ctx, u_gen));
      } else if (Var.is_case(x)) {
        let (u, u_gen) = MetaVarGen.next(u_gen);
        let var = UHPat.Var(NotInHole, InVarHole(Keyword(Case), u), x);
        let zp = ZPat.(ZP0(CursorP(cursor, var)));
        Succeeded((zp, Hole, ctx, u_gen));
      } else {
        check_valid(
          x,
          {
            let ctx = Contexts.extend_gamma(ctx, (x, Hole));
            let zp = ZPat.(ZP0(CursorP(cursor, UHPat.var(x))));
            Succeeded((zp, HTyp.Hole, ctx, u_gen));
          },
        );
      }
    | (Construct(SVar(_)), CursorP(_)) => Failed

    | (
        Construct(SWild),
        CursorP(_, EmptyHole(_) | Wild(_) | Var(_) | NumLit(_) | BoolLit(_)),
      ) =>
      let zp = ZPat.(ZP0(place_after_operand(Wild(NotInHole))));
      Succeeded((zp, Hole, ctx, u_gen));
    | (Construct(SWild), CursorP(_)) => Failed

    | (
        Construct(SNumLit(n, cursor)),
        CursorP(_, EmptyHole(_) | Wild(_) | Var(_) | NumLit(_) | BoolLit(_)),
      ) =>
      let zp = ZPat.ZP0(CursorP(cursor, NumLit(NotInHole, n)));
      Succeeded((zp, Num, ctx, u_gen));
    | (Construct(SNumLit(_, _)), CursorP(_)) => Failed

    | (Construct(SListNil), CursorP(_, EmptyHole(_))) =>
      let zp = ZPat.(ZP0(place_after_operand(ListNil(NotInHole))));
      Succeeded((zp, List(Hole), ctx, u_gen));
    | (Construct(SListNil), CursorP(_, _)) => Failed

    | (Construct(SParenthesized), CursorP(_)) =>
      mk_syn_result(ctx, u_gen, ZPat.ZP0(ParenthesizedZ(ZP0(zoperand))))

    | (Construct(SInj(side)), CursorP(_) as zbody) =>
      let zp = ZPat.ZP0(InjZ(NotInHole, side, ZP0(zbody)));
      switch (Statics.Pat.syn(ctx, zp |> ZPat.erase)) {
      | None => Failed
      | Some((body_ty, ctx)) =>
        let ty =
          switch (side) {
          | L => HTyp.Sum(body_ty, Hole)
          | R => HTyp.Sum(Hole, body_ty)
          };
        Succeeded((zp, ty, ctx, u_gen));
      };

    | (Construct(SOp(os)), CursorP(_)) =>
      switch (operator_of_shape(os)) {
      | None => Failed
      | Some(operator) =>
        let construct_operator =
          ZPat.is_before_zoperand(zoperand)
            ? construct_operator_before_zoperand
            : construct_operator_after_zoperand;
        let (zseq, u_gen) =
          construct_operator(u_gen, operator, zoperand, (E, E));
        Succeeded(mk_and_syn_fix_ZOpSeq(ctx, u_gen, zseq));
      }

    /* Zipper */
    | (_, ParenthesizedZ(zbody)) =>
      switch (syn_perform(ctx, u_gen, a, zbody)) {
      | Failed => Failed
      | CursorEscaped(Before) =>
        syn_perform_operand(ctx, u_gen, MoveLeft, zoperand)
      | CursorEscaped(After) =>
        syn_perform_operand(ctx, u_gen, MoveRight, zoperand)
      | Succeeded((zbody, ty, ctx, u_gen)) =>
        Succeeded((ZP0(ParenthesizedZ(zbody)), ty, ctx, u_gen))
      }
    | (_, InjZ(_, side, zbody)) =>
      switch (syn_perform(ctx, u_gen, a, zbody)) {
      | Failed => Failed
      | CursorEscaped(Before) =>
        syn_perform_operand(ctx, u_gen, MoveLeft, zoperand)
      | CursorEscaped(After) =>
        syn_perform_operand(ctx, u_gen, MoveRight, zoperand)
      | Succeeded((zbody, ty1, ctx, u_gen)) =>
        let zp = ZPat.(ZP0(InjZ(NotInHole, side, zbody)));
        let ty =
          switch (side) {
          | L => HTyp.Sum(ty1, Hole)
          | R => HTyp.Sum(Hole, ty1)
          };
        Succeeded((zp, ty, ctx, u_gen));
      }
    };
  }
  and ana_perform =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, a: t, zp: ZPat.t, ty: HTyp.t)
      : result((ZPat.t, Contexts.t, MetaVarGen.t)) =>
    switch (zp) {
    | ZP1(zp1) => ana_perform_opseq(ctx, u_gen, a, zp1, ty)
    | ZP0(zp0) => ana_perform_operand(ctx, u_gen, a, zp0, ty)
    }
  and ana_perform_opseq =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        a: t,
        ZOpSeq(skel, zseq) as zopseq: ZPat.zopseq,
        ty: HTyp.t,
      )
      : result((ZPat.t, Contexts.t, MetaVarGen.t)) =>
    switch (a, zseq) {
    /* Invalid cursor positions */
    | (_, ZOperator((OnText(_) | OnDelim(_), _), _)) => Failed

    /* Invalid actions */
    | (UpdateApPalette(_), ZOperator(_)) => Failed

    /* Movement */
    | (MoveTo(path), _) =>
      switch (CursorPath.Pat.follow(path, P1(zopseq |> ZPat.erase_zopseq))) {
      | None => Failed
      | Some(zp) => mk_ana_result(ctx, u_gen, zp, ty)
      }
    | (MoveToBefore(steps), _) =>
      switch (
        CursorPath.Pat.follow_steps(
          ~side=Before,
          steps,
          P1(zopseq |> ZPat.erase_zopseq),
        )
      ) {
      | None => Failed
      | Some(zp) => mk_ana_result(ctx, u_gen, zp, ty)
      }
    | (MoveToPrevHole, _) =>
      switch (CursorPath.(prev_hole_steps(Pat.holes_zopseq(zopseq, [])))) {
      | None => Failed
      | Some(steps) =>
        ana_perform_opseq(ctx, u_gen, MoveToBefore(steps), zopseq, ty)
      }
    | (MoveToNextHole, _) =>
      switch (CursorPath.(next_hole_steps(Pat.holes_zopseq(zopseq, [])))) {
      | None => Failed
      | Some(steps) =>
        ana_perform_opseq(ctx, u_gen, MoveToBefore(steps), zopseq, ty)
      }
    | (MoveLeft, _) =>
      switch (ZPat.(ZP1(zopseq) |> move_cursor_left)) {
      | None => CursorEscaped(Before)
      | Some(zp) => mk_ana_result(ctx, u_gen, zp, ty)
      }
    | (MoveRight, _) =>
      switch (ZPat.(ZP1(zopseq) |> move_cursor_right)) {
      | None => CursorEscaped(After)
      | Some(zp) => mk_ana_result(ctx, u_gen, zp, ty)
      }

    /* Deletion */

    | (Delete, ZOperator((OnOp(After), _), _)) =>
      ana_cursor_escaped_zopseq(ctx, u_gen, ty, After, zopseq)
    | (Backspace, ZOperator((OnOp(Before), _), _)) =>
      ana_cursor_escaped_zopseq(ctx, u_gen, ty, Before, zopseq)

    /* Delete before operator == Backspace after operator */
    | (Delete, ZOperator((OnOp(Before), op), surround)) =>
      ana_perform_opseq(
        ctx,
        u_gen,
        Backspace,
        ZOpSeq(skel, ZOperator((OnOp(After), op), surround)),
        ty,
      )

    /* ... + [k-1] +<| [k] + ... */
    | (Backspace, ZOperator((OnOp(After), _), surround)) =>
      let new_zseq = delete_operator(surround);
      Succeeded(mk_and_ana_fix_ZOpSeq(ctx, u_gen, new_zseq, ty));

    /* ... + [k-1]  <|_ + [k+1] + ...  ==>   ... + [k-1]| + [k+1] + ... */
    | (
        Backspace,
        ZOperand(
          CursorP(_, EmptyHole(_)) as zhole,
          (A(Space, prefix_tl), suffix),
        ),
      )
        when ZPat.is_before_zoperand(zhole) =>
      let S(operand, new_prefix) = prefix_tl;
      let zoperand = operand |> ZPat.place_after_operand;
      let new_zseq = ZSeq.ZOperand(zoperand, (new_prefix, suffix));
      Succeeded(mk_and_ana_fix_ZOpSeq(ctx, u_gen, new_zseq, ty));

    /* ... + [k-1] + _|>  [k+1] + ...  ==>   ... + [k-1] + |[k+1] + ... */
    | (
        Delete,
        ZOperand(
          CursorP(_, EmptyHole(_)) as zhole,
          (prefix, A(Space, suffix_tl)),
        ),
      )
        when ZPat.is_after_zoperand(zhole) =>
      let S(operand, new_suffix) = suffix_tl;
      let zoperand = operand |> ZPat.place_before_operand;
      let new_zseq = ZSeq.ZOperand(zoperand, (prefix, new_suffix));
      Succeeded(mk_and_ana_fix_ZOpSeq(ctx, u_gen, new_zseq, ty));

    /* Construction */

    /* construction on operators either becomes movement... */
    | (Construct(SOp(SSpace)), ZOperator(zoperator, _))
        when ZPat.is_after_zoperator(zoperator) =>
      ana_perform_opseq(ctx, u_gen, MoveRight, zopseq, ty)
    /* ...or construction after movement */
    | (Construct(_) as a, ZOperator(zoperator, _)) =>
      let move_cursor =
        ZPat.is_before_zoperator(zoperator)
          ? ZPat.move_cursor_left_zopseq : ZPat.move_cursor_right_zopseq;
      switch (zopseq |> move_cursor) {
      | None => Failed
      | Some(zopseq) => ana_perform_opseq(ctx, u_gen, a, zopseq, ty)
      };

    | (Construct(SOp(os)), ZOperand(zoperand, surround))
        when
          ZPat.is_before_zoperand(zoperand)
          || ZPat.is_after_zoperand(zoperand) =>
      switch (operator_of_shape(os)) {
      | None => Failed
      | Some(operator) =>
        let construct_operator =
          ZPat.is_before_zoperand(zoperand)
            ? construct_operator_before_zoperand
            : construct_operator_after_zoperand;
        let (zseq, u_gen) =
          construct_operator(u_gen, operator, zoperand, surround);
        Succeeded(mk_and_ana_fix_ZOpSeq(ctx, u_gen, zseq, ty));
      }

    /* Zipper */
    | (_, ZOperand(zoperand, surround)) =>
      switch (CursorInfo.Pat.ana_cursor_info_zopseq(ctx, zopseq, ty)) {
      | None => Failed
      | Some(ci) =>
        switch (ci |> CursorInfo.type_mode) {
        | None => Failed
        | Some(Syn) =>
          switch (syn_perform_operand(ctx, u_gen, a, zoperand)) {
          | Failed => Failed
          | CursorEscaped(side) =>
            ana_cursor_escaped_zopseq(ctx, u_gen, ty, side, zopseq)
          | Succeeded((zp, _, _, u_gen)) =>
            let zseq = resurround(zp, surround);
            Succeeded(mk_and_ana_fix_ZOpSeq(ctx, u_gen, zseq, ty));
          }
        | Some(Ana(ty_zoperand)) =>
          switch (ana_perform_operand(ctx, u_gen, a, zoperand, ty_zoperand)) {
          | Failed => Failed
          | CursorEscaped(side) =>
            ana_cursor_escaped_zopseq(ctx, u_gen, ty, side, zopseq)
          | Succeeded((zp, _, u_gen)) =>
            let new_zseq = resurround(zp, surround);
            Succeeded(mk_and_ana_fix_ZOpSeq(ctx, u_gen, zseq, ty));
          }
        }
      }
    }
  and ana_perform_operand =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        a: t,
        zoperand: ZPat.zoperand,
        ty: HTyp.t,
      )
      : result((ZPat.t, Contexts.t, MetaVarGen.t)) =>
    switch (a, zoperand) {
    /* Invalid cursor positions */
    | (
        _,
        CursorP(
          OnText(_),
          EmptyHole(_) | Wild(_) | ListNil(_) | Parenthesized(_) | Inj(_),
        ) |
        CursorP(OnDelim(_), Var(_) | NumLit(_) | BoolLit(_)) |
        CursorP(OnOp(_), _),
      ) =>
      Failed
    | (_, CursorP(cursor, operand))
        when !ZPat.is_valid_cursor_operand(cursor, operand) =>
      Failed

    /* Invalid actions */
    | (
        Construct(
          SApPalette(_) | SNum | SBool | SList | SAsc | SLet | SLine | SLam |
          SCase,
        ) |
        UpdateApPalette(_),
        _,
      ) =>
      Failed

    /* switch to synthesis if in a hole */
    | (_, _) when ZPat.is_inconsistent(ZP0(zoperand)) =>
      let zp = ZPat.ZP0(zoperand);
      let err = zp |> ZPat.erase |> UHPat.get_err_status;
      let zp' = zp |> ZPat.set_err_status(NotInHole);
      let p' = zp' |> ZPat.erase;
      switch (Statics.Pat.syn(ctx, p')) {
      | None => Failed
      | Some(_) =>
        switch (syn_perform(ctx, u_gen, a, zp')) {
        | (Failed | CursorEscaped(_)) as err => err
        | Succeeded((zp, ty', ctx, u_gen)) =>
          if (HTyp.consistent(ty, ty')) {
            Succeeded((zp, ctx, u_gen));
          } else {
            Succeeded((zp |> ZPat.set_err_status(err), ctx, u_gen));
          }
        }
      };

    /* Movement */
    /* NOTE: we don't need to handle movement actions here for the purposes of the UI,
     * since it's handled at the top (expression) level, but for the sake of API completeness
     * we include it */
    | (MoveTo(path), _) =>
      let operand = zoperand |> ZPat.erase_zoperand;
      switch (CursorPath.Pat.follow(path, P0(operand))) {
      | None => Failed
      | Some(zp) => mk_ana_result(ctx, u_gen, zp, ty)
      };
    | (MoveToBefore(steps), _) =>
      let operand = zoperand |> ZPat.erase_zoperand;
      switch (CursorPath.Pat.follow_steps(steps, P0(operand))) {
      | None => Failed
      | Some(zp) => mk_ana_result(ctx, u_gen, zp, ty)
      };
    | (MoveToPrevHole, _) =>
      switch (CursorPath.(prev_hole_steps(Pat.holes_zoperand(zoperand, [])))) {
      | None => Failed
      | Some(steps) =>
        ana_perform_operand(ctx, u_gen, MoveToBefore(steps), zoperand, ty)
      }
    | (MoveToNextHole, _) =>
      switch (CursorPath.(next_hole_steps(Pat.holes_zoperand(zoperand, [])))) {
      | None => Failed
      | Some(steps) =>
        ana_perform_operand(ctx, u_gen, MoveToBefore(steps), zoperand, ty)
      }
    | (MoveLeft, _) =>
      switch (ZPat.(ZP0(zoperand) |> move_cursor_left)) {
      | None => CursorEscaped(Before)
      | Some(zp) => mk_ana_result(ctx, u_gen, zp, ty)
      }
    | (MoveRight, _) =>
      switch (ZPat.(ZP0(zoperand) |> move_cursor_right)) {
      | None => CursorEscaped(After)
      | Some(zp) => mk_ana_result(ctx, u_gen, zp, ty)
      }

    /* Backspace and Delete */

    | (Backspace, _) when ZPat.is_before_zoperand(zoperand) =>
      CursorEscaped(Before)
    | (Delete, _) when ZPat.is_after_zoperand(zoperand) =>
      CursorEscaped(After)

    /* ( _ <|)   ==>   ( _| ) */
    | (Backspace, CursorP(OnDelim(_, Before), _)) =>
      ana_perform_operand(ctx, u_gen, MoveLeft, zoperand, ty)
    /* (|> _ )   ==>   ( |_ ) */
    | (Delete, CursorP(OnDelim(_, After), _)) =>
      ana_perform_operand(ctx, u_gen, MoveRight, zoperand, ty)

    /* Delete before delimiter == Backspace after delimiter */
    | (Delete, CursorP(OnDelim(k, Before), operand)) =>
      ana_perform_operand(
        ctx,
        u_gen,
        Backspace,
        CursorP(OnDelim(k, Before), operand),
        ty,
      )

    | (Backspace, CursorP(_, EmptyHole(_) as operand)) =>
      if (ZPat.is_after_zoperand(zoperand)) {
        let zp = ZPat.(ZP0(place_before_operand(operand)));
        Succeeded((zp, ctx, u_gen));
      } else {
        CursorEscaped(Before);
      }
    | (Delete, CursorP(_, EmptyHole(_) as operand)) =>
      if (ZPat.is_before_zoperand(zoperand)) {
        let zp = ZPat.(ZP0(place_after_operand(operand)));
        Succeeded((zp, ctx, u_gen));
      } else {
        CursorEscaped(After);
      }

    | (
        Backspace | Delete,
        CursorP(
          OnText(_) | OnDelim(_, _),
          Var(_, _, _) | Wild(_) | NumLit(_, _) | BoolLit(_, _) | ListNil(_),
        ),
      ) =>
      let (zhole, u_gen) = ZPat.new_EmptyHole(u_gen);
      let zp = ZPat.ZP0(zhole);
      Succeeded((zp, ctx, u_gen));

    /* ( _ )<|  ==>  _| */
    /* (<| _ )  ==>  |_ */
    | (
        Backspace,
        CursorP(OnDelim(k, After), Parenthesized(body) | Inj(_, _, body)),
      ) =>
      let place_cursor = k == 0 ? ZPat.place_before : ZPat.place_after;
      Succeeded(
        Statics.Pat.ana_fix_holes_z(ctx, u_gen, body |> place_cursor, ty),
      );

    /* Construct */
    | (Construct(SOp(SSpace)), CursorP(OnDelim(_, After), _)) =>
      ana_perform_operand(ctx, u_gen, MoveRight, zoperand, ty)
    | (Construct(_) as a, CursorP(OnDelim(_, side), _))
        when
          !ZPat.is_before_zoperand(zoperand)
          && !ZPat.is_after_zoperand(zoperand) =>
      let move_then_perform = move_action =>
        switch (ana_perform_operand(ctx, u_gen, move_action, zoperand, ty)) {
        | Failed
        | CursorEscaped(_) => assert(false)
        | Succeeded((zp, _, u_gen)) => ana_perform(ctx, u_gen, a, zp, ty)
        };
      switch (side) {
      | Before => move_then_perform(MoveLeft)
      | After => move_then_perform(MoveRight)
      };

    | (Construct(SVar("true", _)), _)
    | (Construct(SVar("false", _)), _) =>
      switch (syn_perform_operand(ctx, u_gen, a, zoperand)) {
      | (Failed | CursorEscaped(_)) as err => err
      | Succeeded((zp, ty', ctx, u_gen)) =>
        if (HTyp.consistent(ty, ty')) {
          Succeeded((zp, ctx, u_gen));
        } else {
          let (zp, u_gen) = zp |> ZPat.make_inconsistent(u_gen);
          Succeeded((zp, ctx, u_gen));
        }
      }

    | (
        Construct(SVar(x, cursor)),
        CursorP(_, EmptyHole(_) | Wild(_) | Var(_) | NumLit(_) | BoolLit(_)),
      ) =>
      if (Var.is_let(x)) {
        let (u, u_gen) = u_gen |> MetaVarGen.next;
        let var = UHPat.var(~var_err=InVarHole(Keyword(Let), u), x);
        let zp = ZPat.ZP0(CursorP(cursor, var));
        Succeeded((zp, ctx, u_gen));
      } else if (Var.is_case(x)) {
        let (u, u_gen) = u_gen |> MetaVarGen.next;
        let var = UHPat.var(~var_err=InVarHole(Keyword(Case), u), x);
        let zp = ZPat.ZP0(CursorP(cursor, var));
        Succeeded((zp, ctx, u_gen));
      } else {
        check_valid(
          x,
          {
            let ctx = Contexts.extend_gamma(ctx, (x, ty));
            let zp = ZPat.ZP0(CursorP(cursor, UHPat.var(x)));
            Succeeded((zp, ctx, u_gen));
          },
        );
      }
    | (Construct(SVar(_)), CursorP(_)) => Failed

    | (
        Construct(SWild),
        CursorP(_, EmptyHole(_) | Wild(_) | Var(_) | NumLit(_) | BoolLit(_)),
      ) =>
      Succeeded((ZPat.place_after(P0(UHPat.wild())), ctx, u_gen))
    | (Construct(SWild), CursorP(_)) => Failed

    | (Construct(SParenthesized), CursorP(_)) =>
      mk_ana_result(
        ctx,
        u_gen,
        ZPat.ZP0(ParenthesizedZ(ZP0(zoperand))),
        ty,
      )

    | (Construct(SInj(side)), CursorP(_)) =>
      switch (HTyp.matched_sum(ty)) {
      | Some((tyL, tyR)) =>
        let body_ty = InjSide.pick(side, tyL, tyR);
        let (zbody, ctx, u_gen) =
          Statics.Pat.ana_fix_holes_z(ctx, u_gen, ZP0(zoperand), body_ty);
        let zp = ZPat.ZP0(InjZ(NotInHole, side, zbody));
        Succeeded((zp, ctx, u_gen));
      | None =>
        let (zbody, _, ctx, u_gen) =
          Statics.Pat.syn_fix_holes_z(ctx, u_gen, ZP0(zoperand));
        let (u, u_gen) = u_gen |> MetaVarGen.next;
        let zp = ZPat.ZP0(InjZ(InHole(TypeInconsistent, u), side, zbody));
        Succeeded((zp, ctx, u_gen));
      }

    | (Construct(SOp(os)), CursorP(_)) =>
      switch (operator_of_shape(os)) {
      | None => Failed
      | Some(operator) =>
        let construct_operator =
          ZPat.is_before_zoperand(zoperand)
            ? construct_operator_before_zoperand
            : construct_operator_after_zoperand;
        let (zseq, u_gen) =
          construct_operator(u_gen, operator, zoperand, (E, E));
        Succeeded(mk_and_ana_fix_ZOpSeq(ctx, u_gen, zseq, ty));
      }

    /* Zipper */
    | (_, ParenthesizedZ(zbody)) =>
      switch (ana_perform(ctx, u_gen, a, zbody, ty)) {
      | Failed => Failed
      | CursorEscaped(Before) =>
        ana_perform_operand(ctx, u_gen, MoveLeft, zoperand, ty)
      | CursorEscaped(After) =>
        ana_perform_operand(ctx, u_gen, MoveRight, zoperand, ty)
      | Succeeded((zbody, ctx, u_gen)) =>
        let zp = ZPat.ZP0(ParenthesizedZ(zbody));
        Succeeded((zp, ctx, u_gen));
      }
    | (_, InjZ(_, side, zbody)) =>
      switch (HTyp.matched_sum(ty)) {
      | None => Failed
      | Some((tyL, tyR)) =>
        let body_ty = InjSide.pick(side, tyL, tyR);
        switch (ana_perform(ctx, u_gen, a, zbody, body_ty)) {
        | Failed => Failed
        | CursorEscaped(Before) =>
          ana_perform_operand(ctx, u_gen, MoveLeft, zoperand, ty)
        | CursorEscaped(After) =>
          ana_perform_operand(ctx, u_gen, MoveRight, zoperand, ty)
        | Succeeded((zbody, ctx, u_gen)) =>
          let zp = ZPat.ZP0(InjZ(NotInHole, side, zbody));
          Succeeded((zp, ctx, u_gen));
        };
      }

    /* Subsumption */
    | (Construct(SNumLit(_, _)), _)
    | (Construct(SListNil), _) =>
      switch (syn_perform_operand(ctx, u_gen, a, zoperand)) {
      | (Failed | CursorEscaped(_)) as err => err
      | Succeeded((zp, ty', ctx, u_gen)) =>
        if (HTyp.consistent(ty, ty')) {
          Succeeded((zp, ctx, u_gen));
        } else {
          let (zp, u_gen) = zp |> ZPat.make_inconsistent(u_gen);
          Succeeded((zp, ctx, u_gen));
        }
      }
    };
};

let combine_for_Backspace_Space = (e1: UHExp.t, ze0: ZExp.t): ZExp.t =>
  switch (e1, ze0) {
  | (_, CursorE(_, EmptyHole(_))) =>
    /* e1 |_ --> e1| */
    ZExp.place_after_operand(e1)
  | _ => ze0
  };

let combine_for_Delete_Space = (ze0: ZExp.t, e: UHExp.t): ZExp.t =>
  switch (ze0, e) {
  | (CursorE(_, EmptyHole(_)), EmptyHole(_))
      when ZExp.is_after_zoperand(ze0) =>
    /* _| _ --> _| */
    ze0
  | (CursorE(_, EmptyHole(_)), _) when ZExp.is_after_zoperand(ze0) =>
    /* _| e --> |e */
    ZExp.place_before_operand(e)
  | _ => ze0
  };

/**
 * Used to construct an expression from an opseq suffix that
 * follows a keyword when the user hits space after the keyword.
 * If the first operation is a space, then what follows the space
 * becomes the new expression. Otherwise, a new hole is generated,
 * prepended to the suffix, and the reuslting opseq becomes the
 * new expression.
 */
let keyword_suffix_to_exp =
    (suffix: Seq.suffix(UHExp.t, UHExp.operator), u_gen: MetaVarGen.t)
    : (UHExp.t, MetaVarGen.t) =>
  switch (suffix) {
  | OperandSuffix(Space, e) => (e, u_gen)
  | SeqSuffix(Space, seq) => (
      OpSeq(Associator.associate_exp(seq), seq),
      u_gen,
    )
  | OperandSuffix(_, _)
  | SeqSuffix(_, _) =>
    let (hole, u_gen) = UHExp.new_EmptyHole(u_gen);
    let opseq = Seq.t_of_first_and_suffix(hole, suffix);
    let skel = Associator.associate_exp(opseq);
    (OpSeq(skel, opseq), u_gen);
  };

let keyword_action = (kw: Keyword.t): t =>
  switch (kw) {
  | Let => Construct(SLet)
  | Case => Construct(SCase)
  };

type zexp_or_zblock = ZExp.zexp_or_zblock;

let set_err_status_zexp_or_zblock =
    (err: ErrStatus.t, ze_zb: zexp_or_zblock): zexp_or_zblock =>
  switch (ze_zb) {
  | E(ze) => E(ZExp.set_err_status_operand(err, ze))
  | B(zblock) => B(ZExp.set_err_status_zblock(err, zblock))
  };

let make_zexp_or_zblock_inconsistent =
    (u_gen: MetaVarGen.t, ze_zb: zexp_or_zblock)
    : (zexp_or_zblock, MetaVarGen.t) =>
  switch (ze_zb) {
  | E(ze) =>
    let (ze, u_gen) = ZExp.make_inconsistent(u_gen, ze);
    (E(ze), u_gen);
  | B(zblock) =>
    let (zblock, u_gen) = ZExp.make_inconsistent_zblock(u_gen, zblock);
    (B(zblock), u_gen);
  };

let rec syn_perform_block =
        (
          ~ci: CursorInfo.t,
          ctx: Contexts.t,
          a: t,
          (zblock, ty, u_gen): (ZExp.zblock, HTyp.t, MetaVarGen.t),
        )
        : result((ZExp.zblock, HTyp.t, MetaVarGen.t)) =>
  switch (a, zblock) {
  /* Staging */
  | (
      ShiftUp | ShiftDown,
      BlockZL(
        (prefix, CursorL(Staging(3), LetLine(p, ann, def)), suffix),
        e,
      ),
    ) =>
    let shift_line =
      switch (a) {
      | ShiftUp => UHExp.shift_line_to_suffix_block
      | _ => UHExp.shift_line_from_suffix_block(~is_node_terminal=false)
      };
    switch (def |> shift_line(~u_gen, Some(Block(suffix, e)))) {
    | None => CantShift
    | Some((_, None, _)) =>
      // should not happen since let line is not terminal
      assert(false)
    | Some((new_def, Some(Block(new_suffix, new_e)), u_gen)) =>
      let new_zblock =
        ZExp.BlockZL(
          (
            prefix,
            CursorL(Staging(3), LetLine(p, ann, new_def)),
            new_suffix,
          ),
          new_e,
        );
      Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, new_zblock));
    };
  | (
      ShiftUp | ShiftDown | ShiftLeft | ShiftRight,
      BlockZL((_, CursorL(Staging(_), LetLine(_, _, _)), _), _),
    ) =>
    CantShift
  | (
      ShiftUp | ShiftDown | ShiftLeft | ShiftRight,
      BlockZL((_, CursorL(Staging(_), EmptyLine | ExpLine(_)), _), _),
    ) =>
    Failed
  | (
      ShiftUp | ShiftDown,
      BlockZL(
        (
          prefix,
          ExpLineZ(
            CursorE(
              Staging(0) as cursor,
              (
                Parenthesized(block) | Inj(_, _, block) |
                Case(_, block, _, _)
              ) as e_line,
            ),
          ),
          suffix,
        ),
        e,
      ),
    ) =>
    let shift_line =
      switch (a) {
      | ShiftUp => UHExp.shift_line_from_prefix
      | _ => UHExp.shift_line_to_prefix
      };
    switch (block |> shift_line(~u_gen, prefix)) {
    | None => CantShift
    | Some((new_prefix, new_block, u_gen)) =>
      let new_e_line =
        switch (e_line) {
        | Inj(err, side, _) => UHExp.Inj(err, side, new_block)
        | Case(err, _, rules, ann) => Case(err, new_block, rules, ann)
        | _ => Parenthesized(new_block)
        };
      let new_zblock =
        ZExp.BlockZL(
          (new_prefix, ExpLineZ(CursorE(cursor, new_e_line)), suffix),
          e,
        );
      Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, new_zblock));
    };
  | (
      ShiftUp | ShiftDown,
      BlockZL(
        (
          prefix,
          ExpLineZ(
            CursorE(
              Staging(1) as cursor,
              (Parenthesized(block) | Inj(_, _, block)) as e_line,
            ),
          ),
          suffix,
        ),
        e,
      ),
    ) =>
    let shift_line =
      switch (a) {
      | ShiftUp => UHExp.shift_line_to_suffix_block
      | _ => UHExp.shift_line_from_suffix_block(~is_node_terminal=true)
      };
    switch (block |> shift_line(~u_gen, Some(Block(suffix, e)))) {
    | None => CantShift
    | Some((new_block, None, u_gen)) =>
      let new_conclusion: UHExp.t =
        switch (e_line) {
        | Inj(err, side, _) => Inj(err, side, new_block)
        | _ => Parenthesized(new_block)
        };
      let new_zblock = ZExp.BlockZE(prefix, CursorE(cursor, new_conclusion));
      Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, new_zblock));
    | Some((new_block, Some(Block(new_suffix, new_e)), u_gen)) =>
      let new_e_line: UHExp.t =
        switch (e_line) {
        | Inj(err, side, _) => Inj(err, side, new_block)
        | _ => Parenthesized(new_block)
        };
      let new_zblock =
        ZExp.BlockZL(
          (prefix, ExpLineZ(CursorE(cursor, new_e_line)), new_suffix),
          new_e,
        );
      Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, new_zblock));
    };
  | (
      ShiftUp | ShiftDown,
      BlockZL(
        (
          prefix,
          ExpLineZ(
            CursorE(Staging(1) as cursor, Case(err, scrut, rules, None)),
          ),
          suffix,
        ),
        e,
      ),
    ) =>
    switch (rules |> split_last) {
    | None => Failed // shouldn't ever see empty rule list
    | Some((leading_rules, Rule(last_p, last_clause))) =>
      let shift_line =
        switch (a) {
        | ShiftUp => UHExp.shift_line_to_suffix_block
        | _ => UHExp.shift_line_from_suffix_block(~is_node_terminal=true)
        };
      switch (last_clause |> shift_line(~u_gen, Some(Block(suffix, e)))) {
      | None => CantShift
      | Some((new_last_clause, None, u_gen)) =>
        let new_e =
          UHExp.Case(
            err,
            scrut,
            leading_rules @ [Rule(last_p, new_last_clause)],
            None,
          );
        let new_zblock = ZExp.BlockZE(prefix, CursorE(cursor, new_e));
        Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, new_zblock));
      | Some((new_last_clause, Some(Block(new_suffix, new_e)), u_gen)) =>
        let new_e_line =
          UHExp.Case(
            err,
            scrut,
            leading_rules @ [Rule(last_p, new_last_clause)],
            None,
          );
        let new_zblock =
          ZExp.BlockZL(
            (prefix, ExpLineZ(CursorE(cursor, new_e_line)), new_suffix),
            new_e,
          );
        Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, new_zblock));
      };
    }
  | (
      ShiftRight,
      BlockZE(
        leading,
        CursorE(
          Staging(0) as cursor,
          (Parenthesized(Block([], body)) | Inj(_, _, Block([], body))) as staged,
        ),
      ),
    ) =>
    switch (body |> OpSeqUtil.Exp.shift_optm_to_prefix(~surround=None)) {
    | None => CantShift
    | Some((new_body, new_surround)) =>
      let new_ztm =
        ZExp.CursorE(
          cursor,
          switch (staged) {
          | Inj(err, side, _) =>
            Inj(err, side, new_body |> UHExp.wrap_in_block)
          | _parenthesized => Parenthesized(new_body |> UHExp.wrap_in_block)
          },
        );
      let new_ze =
        switch (new_surround) {
        | None => new_ztm
        | Some(surround) => OpSeqUtil.Exp.mk_ZOpSeq(new_ztm, surround)
        };
      let new_zblock = ZExp.BlockZE(leading, new_ze);
      Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, new_zblock));
    }
  | (
      ShiftUp,
      BlockZL(
        (
          leading_prefix,
          ExpLineZ(
            OpSeqZ(
              _,
              CursorE(Staging(1) as cursor, Parenthesized(Block([], body))),
              EmptySuffix(prefix),
            ),
          ),
          leading_suffix,
        ),
        conclusion,
      ),
    ) =>
    // skip over remaining left shifts, then apply ShiftUp to result
    let skipped_body = OpSeqUtil.Exp.prepend(prefix, body);
    let skipped_zblock =
      ZExp.BlockZL(
        (
          leading_prefix,
          ExpLineZ(
            CursorE(cursor, Parenthesized(Block([], skipped_body))),
          ),
          leading_suffix,
        ),
        conclusion,
      );
    syn_perform_block(
      ~ci,
      ctx,
      ShiftUp,
      Statics.Exp.syn_fix_holes_z(ctx, u_gen, skipped_zblock),
    );
  | (
      ShiftDown,
      BlockZL(
        (
          leading_prefix,
          ExpLineZ(
            OpSeqZ(
              _,
              CursorE(Staging(1) as cursor, Parenthesized(Block([], body))),
              EmptyPrefix(suffix),
            ),
          ),
          leading_suffix,
        ),
        conclusion,
      ),
    ) =>
    // skip over remaining right shifts, then apply ShiftDown to result
    let skipped_body = OpSeqUtil.Exp.append(body, suffix);
    let skipped_zblock =
      ZExp.BlockZL(
        (
          leading_prefix,
          ExpLineZ(
            CursorE(cursor, Parenthesized(Block([], skipped_body))),
          ),
          leading_suffix,
        ),
        conclusion,
      );
    syn_perform_block(
      ~ci,
      ctx,
      ShiftDown,
      Statics.Exp.syn_fix_holes_z(ctx, u_gen, skipped_zblock),
    );
  | (
      ShiftUp,
      BlockZE(
        leading,
        OpSeqZ(
          _,
          CursorE(Staging(1) as cursor, Parenthesized(Block([], body))),
          EmptySuffix(prefix),
        ),
      ),
    ) =>
    // skip over remaining left shifts, then apply ShiftUp to result
    let skipped_body = OpSeqUtil.Exp.prepend(prefix, body);
    let skipped_zblock =
      ZExp.BlockZE(
        leading,
        CursorE(cursor, Parenthesized(Block([], skipped_body))),
      );
    syn_perform_block(
      ~ci,
      ctx,
      ShiftUp,
      Statics.Exp.syn_fix_holes_z(ctx, u_gen, skipped_zblock),
    );
  | (
      ShiftUp,
      BlockZE(leading, CursorE(Staging(1) as cursor, Parenthesized(body))),
    ) =>
    switch (body |> UHExp.shift_line_to_suffix_block(~u_gen, None)) {
    | None => CantShift
    | Some((_, None, _)) => assert(false)
    | Some((
        new_body,
        Some(Block(new_suffix_leading, new_suffix_conclusion)),
        u_gen,
      )) =>
      let new_zblock =
        ZExp.BlockZL(
          (
            leading,
            ExpLineZ(CursorE(cursor, Parenthesized(new_body))),
            new_suffix_leading,
          ),
          new_suffix_conclusion,
        );
      Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, new_zblock));
    }
  | (
      ShiftLeft,
      BlockZE(
        leading,
        CursorE(
          Staging(1) as cursor,
          (Parenthesized(Block([], body)) | Inj(_, _, Block([], body))) as staged,
        ),
      ),
    ) =>
    switch (body |> OpSeqUtil.Exp.shift_optm_to_suffix(~surround=None)) {
    | None => CantShift
    | Some((new_body, new_surround)) =>
      let new_ztm =
        ZExp.CursorE(
          cursor,
          switch (staged) {
          | Inj(err, side, _) =>
            Inj(err, side, new_body |> UHExp.wrap_in_block)
          | _parenthesized => Parenthesized(new_body |> UHExp.wrap_in_block)
          },
        );
      let new_ze =
        switch (new_surround) {
        | None => new_ztm
        | Some(surround) => OpSeqUtil.Exp.mk_ZOpSeq(new_ztm, surround)
        };
      let new_zblock = ZExp.BlockZE(leading, new_ze);
      Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, new_zblock));
    }
  | (
      ShiftUp | ShiftDown,
      BlockZE(
        leading,
        CursorE(
          Staging(0) as cursor,
          (Parenthesized(block) | Inj(_, _, block) | Case(_, block, _, _)) as conclusion,
        ),
      ),
    ) =>
    let shift_line =
      switch (a) {
      | ShiftUp => UHExp.shift_line_from_prefix
      | _ => UHExp.shift_line_to_prefix
      };
    switch (block |> shift_line(~u_gen, leading)) {
    | None => CantShift
    | Some((new_leading, new_block, u_gen)) =>
      let new_conclusion =
        switch (conclusion) {
        | Inj(err, side, _) => UHExp.Inj(err, side, new_block)
        | Case(err, _, rules, ann) => Case(err, new_block, rules, ann)
        | _ => Parenthesized(new_block)
        };
      let new_zblock =
        ZExp.BlockZE(new_leading, CursorE(cursor, new_conclusion));
      Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, new_zblock));
    };
  /* Movement */
  | (MoveTo(path), _) =>
    let block = ZExp.erase_zblock(zblock);
    switch (CursorPath.follow_block(path, block)) {
    | None => Failed
    | Some(zblock) => Succeeded((zblock, ty, u_gen))
    };
  | (MoveToBefore(steps), _) =>
    let block = ZExp.erase_zblock(zblock);
    switch (CursorPath.follow_block_and_place_before(steps, block)) {
    | None => Failed
    | Some(zblock) => Succeeded((zblock, ty, u_gen))
    };
  | (MoveToPrevHole, _) =>
    switch (CursorPath.Exp.prev_hole_steps_z(zblock)) {
    | None => Failed
    | Some(path) =>
      syn_perform_block(~ci, ctx, MoveTo(path), (zblock, ty, u_gen))
    }
  | (MoveToNextHole, _) =>
    switch (CursorPath.Exp.next_hole_steps_z(zblock)) {
    | None => Failed
    | Some(path) =>
      syn_perform_block(~ci, ctx, MoveTo(path), (zblock, ty, u_gen))
    }
  | (MoveLeft, _) =>
    ZExp.move_cursor_left_zblock(zblock)
    |> Opt.map_default(~default=CursorEscaped(Before), zblock =>
         Succeeded((zblock, ty, u_gen))
       )
  | (MoveRight, _) =>
    ZExp.move_cursor_right_zblock(zblock)
    |> Opt.map_default(~default=CursorEscaped(After), zblock =>
         Succeeded((zblock, ty, u_gen))
       )
  /* Backspace & Delete */
  | (Backspace, _) when ZExp.is_before_zblock(zblock) =>
    CursorEscaped(Before)
  | (Delete, _) when ZExp.is_after_zblock(zblock) => CursorEscaped(After)
  | (Delete, BlockZL((prefix, CursorL(_, EmptyLine), []), e)) =>
    let ze = ZExp.place_before_operand(e);
    let zblock = ZExp.BlockZE(prefix, ze);
    Succeeded((zblock, ty, u_gen));
  | (Backspace, BlockZE(leading, zconclusion))
      when ZExp.is_before_zoperand(zconclusion) =>
    switch (leading |> split_last, zconclusion |> ZExp.erase_zoperand) {
    | (None, _) => CursorEscaped(Before)
    | (Some((leading_prefix, EmptyLine)), _) =>
      Succeeded((BlockZE(leading_prefix, zconclusion), ty, u_gen))
    | (Some((leading_prefix, ExpLine(e))), EmptyHole(_)) =>
      let new_zconclusion = ZExp.place_after_operand(e);
      Succeeded(
        Statics.Exp.syn_fix_holes_z(
          ctx,
          u_gen,
          BlockZE(leading_prefix, new_zconclusion),
        ),
      );
    | (Some((leading_prefix, leading_last)), conclusion) =>
      let zleading_last = ZExp.place_after_line(leading_last);
      let zblock =
        ZExp.BlockZL((leading_prefix, zleading_last, []), conclusion);
      Succeeded((zblock, ty, u_gen));
    }
  | (Delete, BlockZL((prefix, ExpLineZ(ze), []), EmptyHole(_)))
      when ZExp.is_after_zoperand(ze) =>
    switch (Statics.Exp.syn_operand(ctx, ZExp.erase_zoperand(ze))) {
    | None => Failed
    | Some(ty) =>
      let zblock = ZExp.BlockZE(prefix, ze);
      Succeeded((zblock, ty, u_gen));
    }
  | (
      Backspace | Delete,
      BlockZL((prefix, CursorL(Staging(k), _), suffix), conclusion),
    ) =>
    let new_zblock: option(ZExp.zblock) =
      switch (ci |> CursorInfo.preserved_child_term_of_node, suffix) {
      | (Some((_, Type(_) | Pattern(_))), _) => None
      | (None, []) =>
        // If deleted line is followed by an empty hole,
        // then they are on the same visual line. Don't bother
        // leaving behind an empty line, instead let the
        // the empty hole take the deleted line's place.
        switch (conclusion) {
        | EmptyHole(_) =>
          Some(BlockZE(prefix, conclusion |> ZExp.place_before_operand))
        | _ =>
          Some(
            BlockZL(
              (prefix, ZExp.place_before_line(EmptyLine), []),
              conclusion,
            ),
          )
        }
      | (None, [_, ..._]) =>
        Some(
          BlockZL(
            (prefix, ZExp.place_before_line(EmptyLine), suffix),
            conclusion,
          ),
        )
      | (Some((_, Expression(block))), _) =>
        let place_cursor =
          // here we're depending on the fact that
          // only let lines can preserve children
          switch (k) {
          | 0
          | 1
          | 2 => ZExp.place_before_block
          | _three => ZExp.place_after_block
          };
        let (inner_prefix, zline, inner_suffix) =
          block |> place_cursor |> ZExp.prune_empty_hole_lines;
        Some(
          BlockZL(
            (prefix @ inner_prefix, zline, inner_suffix @ suffix),
            conclusion,
          ),
        );
      };
    new_zblock
    |> Opt.map_default(~default=Failed, zblock =>
         Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, zblock))
       );
  /* Construction */
  | (
      Construct(SLine),
      BlockZL((prefix, CursorL(Staging(k), line), suffix), e),
    ) =>
    Succeeded(
      Statics.Exp.syn_fix_holes_z(
        ctx,
        u_gen,
        BlockZL((prefix, CursorL(OnDelim(k, After), line), suffix), e),
      ),
    )
  | (
      Construct(SLine),
      BlockZL((prefix, ExpLineZ(CursorE(Staging(k), e_line)), suffix), e),
    ) =>
    Succeeded(
      Statics.Exp.syn_fix_holes_z(
        ctx,
        u_gen,
        BlockZL(
          (prefix, ExpLineZ(CursorE(OnDelim(k, After), e_line)), suffix),
          e,
        ),
      ),
    )
  | (Construct(_), BlockZL((_, CursorL(Staging(_), _), _), _)) => Failed
  | (Construct(SLine), BlockZE(lines, ze)) when ZExp.is_before_zoperand(ze) =>
    let zblock = ZExp.BlockZE(lines @ [EmptyLine], ze);
    Succeeded((zblock, ty, u_gen));
  | (Construct(SLine), BlockZE(lines, ze)) when ZExp.is_after_zoperand(ze) =>
    let (zhole, u_gen) = ZExp.new_EmptyHole(u_gen);
    let line =
      UHExp.prune_empty_hole_line(ExpLine(ZExp.erase_zoperand(ze)));
    let zblock = ZExp.BlockZE(lines @ [line], zhole);
    Succeeded((zblock, Hole, u_gen));
  | (Construct(SLet), BlockZE(lines, ze1))
      when ZExp.is_before_zoperand(ze1) =>
    let (zp, u_gen) = ZPat.new_EmptyHole(u_gen);
    let e1 = ZExp.erase_zoperand(ze1);
    let zline = ZExp.LetLineZP(zp, None, UHExp.wrap_in_block(e1));
    let zlines = (lines, zline, []);
    let (e2, u_gen) = UHExp.new_EmptyHole(u_gen);
    let zblock = ZExp.BlockZL(zlines, e2);
    Succeeded((zblock, HTyp.Hole, u_gen));
  | (
      Construct(SCase),
      BlockZL(
        (prefix, (CursorL(_, EmptyLine) | ExpLineZ(_)) as zline, suffix),
        e2,
      ),
    )
      when ZExp.is_before_zline(zline) =>
    let (e1, u_gen) =
      switch (zline) {
      | ExpLineZ(ze1) => (ZExp.erase_zoperand(ze1), u_gen)
      | _ =>
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (EmptyHole(u), u_gen);
      };
    let rule_block = UHExp.Block(suffix, e2);
    let (ze, u_gen) =
      switch (e1) {
      | EmptyHole(_) =>
        let (p, u_gen) = UHPat.new_EmptyHole(u_gen);
        let rule = UHExp.Rule(p, rule_block);
        let scrut_zblock = ZExp.BlockZE([], ZExp.place_before_operand(e1));
        (ZExp.CaseZE(NotInHole, scrut_zblock, [rule], Some(Hole)), u_gen);
      | _ =>
        let (zp, u_gen) = ZPat.new_EmptyHole(u_gen);
        let zrule = ZExp.RuleZP(zp, rule_block);
        let zrules = ZList.singleton(zrule);
        let scrut_block = UHExp.wrap_in_block(e1);
        (ZExp.CaseZR(NotInHole, scrut_block, zrules, Some(Hole)), u_gen);
      };
    let zblock = ZExp.BlockZE(prefix, ze);
    Succeeded((zblock, Hole, u_gen));
  | (
      Construct(SOp(SSpace)),
      BlockZL(
        (
          prefix,
          ExpLineZ(
            OpSeqZ(
              _,
              CursorE(_, Var(_, InVarHole(Keyword(k), _), _)) as ze0,
              EmptyPrefix(suffix),
            ),
          ),
          suffix,
        ),
        e2,
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    let (e, u_gen) = keyword_suffix_to_exp(suffix, u_gen);
    let ze = ZExp.place_before_operand(e);
    let zlines = (prefix, ZExp.ExpLineZ(ze), suffix);
    let zblock = ZExp.BlockZL(zlines, e2);
    syn_perform_block(~ci, ctx, keyword_action(k), (zblock, ty, u_gen));
  | (
      Construct(SOp(SSpace)),
      BlockZL(
        (
          prefix,
          ExpLineZ(CursorE(_, Var(_, InVarHole(Keyword(k), _), _)) as ze0),
          suffix,
        ),
        e2,
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    let zlines = (prefix, ZExp.place_before_line(EmptyLine), suffix);
    let zblock = ZExp.BlockZL(zlines, e2);
    syn_perform_block(~ci, ctx, keyword_action(k), (zblock, ty, u_gen));
  | (
      Construct(SOp(SSpace)),
      BlockZE(
        lines,
        OpSeqZ(
          _,
          CursorE(_, Var(_, InVarHole(Keyword(k), _), _)) as ze0,
          EmptyPrefix(suffix),
        ),
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    let (e, u_gen) = keyword_suffix_to_exp(suffix, u_gen);
    switch (Statics.Exp.syn(ctx, e)) {
    | None => Failed
    | Some(ty) =>
      let ze = ZExp.place_before_operand(e);
      let zblock = ZExp.BlockZE(lines, ze);
      syn_perform_block(~ci, ctx, keyword_action(k), (zblock, ty, u_gen));
    };
  | (
      Construct(SOp(SSpace)),
      BlockZE(
        lines,
        CursorE(_, Var(_, InVarHole(Keyword(k), _), _)) as ze0,
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    let (ze, u_gen) = ZExp.new_EmptyHole(u_gen);
    let zblock = ZExp.BlockZE(lines, ze);
    syn_perform_block(~ci, ctx, keyword_action(k), (zblock, Hole, u_gen));
  /* Zipper Cases */
  | (
      Backspace | Delete | Construct(_) | UpdateApPalette(_) | ShiftLeft |
      ShiftRight |
      ShiftUp |
      ShiftDown,
      BlockZL(zlines, e),
    ) =>
    switch (syn_perform_lines(~ci, ctx, a, (zlines, u_gen))) {
    | Failed => Failed
    | CantShift => CantShift
    | CursorEscaped(Before) =>
      syn_perform_block(~ci, ctx, MoveLeft, (zblock, ty, u_gen))
    | CursorEscaped(After) =>
      syn_perform_block(~ci, ctx, MoveRight, (zblock, ty, u_gen))
    | Succeeded((zlines, ctx, u_gen)) =>
      let (e, ty, u_gen) = Statics.syn_fix_holes_exp(ctx, u_gen, e);
      let zblock = ZExp.BlockZL(zlines, e);
      Succeeded((zblock, ty, u_gen));
    }
  | (
      Backspace | Delete | Construct(_) | UpdateApPalette(_) | ShiftLeft |
      ShiftRight |
      ShiftUp |
      ShiftDown,
      BlockZE(lines, ze),
    ) =>
    switch (Statics.syn_lines(ctx, lines)) {
    | None => Failed
    | Some(ctx) =>
      switch (syn_perform_exp(~ci, ctx, a, (ze, ty, u_gen))) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        syn_perform_block(~ci, ctx, MoveLeft, (zblock, ty, u_gen))
      | CursorEscaped(After) =>
        syn_perform_block(~ci, ctx, MoveRight, (zblock, ty, u_gen))
      | Succeeded((E(ze), ty, u_gen)) =>
        Succeeded((BlockZE(lines, ze), ty, u_gen))
      | Succeeded((B(zblock), _, u_gen)) =>
        switch (zblock) {
        | BlockZL((prefix, zline, suffix), e) =>
          let zblock = ZExp.BlockZL((lines @ prefix, zline, suffix), e);
          Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, zblock));
        | BlockZE(ls, ze) =>
          let zblock = ZExp.BlockZE(lines @ ls, ze);
          Succeeded(Statics.Exp.syn_fix_holes_z(ctx, u_gen, zblock));
        }
      }
    }
  }
and syn_perform_lines =
    (
      ~ci: CursorInfo.t,
      ctx: Contexts.t,
      a: t,
      (zlines, u_gen) as edit_state: (ZExp.zlines, MetaVarGen.t),
    )
    : result((ZExp.zlines, Contexts.t, MetaVarGen.t)) =>
  switch (a, zlines) {
  /* Staging */
  | (ShiftUp | ShiftDown | ShiftLeft | ShiftRight, (_, CursorL(_, _), _)) =>
    // handled at block level
    Failed
  /* Movement */
  | (MoveTo(_), _)
  | (MoveToBefore(_), _)
  | (MoveToPrevHole, _)
  | (MoveToNextHole, _) =>
    /* TODO implement when we have cells, which
     * will be modeled as lists of lines
     */
    Failed
  /* Backspace & Delete */
  | (Backspace, _) when ZExp.is_before_zlines(zlines) =>
    CursorEscaped(Before)
  | (Delete, _) when ZExp.is_after_zlines(zlines) => CursorEscaped(After)
  | (Delete, (prefix, CursorL(_, EmptyLine), suffix)) =>
    switch (suffix) {
    | [] => Failed
    | [line, ...suffix] =>
      let zlines = (prefix, ZExp.place_before_line(line), suffix);
      switch (Statics.syn_zlines(ctx, zlines)) {
      | None => Failed
      | Some(ctx) => Succeeded((zlines, ctx, u_gen))
      };
    }
  | (Backspace, (prefix, CursorL(_, EmptyLine), suffix)) =>
    switch (split_last(prefix)) {
    | None => Failed
    | Some((prefix, line)) =>
      let zlines = (prefix, ZExp.place_after_line(line), suffix);
      switch (Statics.syn_zlines(ctx, zlines)) {
      | None => Failed
      | Some(ctx) => Succeeded((zlines, ctx, u_gen))
      };
    }
  | (Delete, (prefix, zline1, suffix)) when ZExp.is_after_zline(zline1) =>
    switch (suffix) {
    | [] => Failed
    | [line2, ...suffix] =>
      switch (line2) {
      | ExpLine(_) => Failed
      | LetLine(_, _, _) => Failed
      | EmptyLine =>
        let zlines = (prefix, zline1, suffix);
        switch (Statics.syn_zlines(ctx, zlines)) {
        | None => Failed
        | Some(ctx) => Succeeded((zlines, ctx, u_gen))
        };
      }
    }
  | (Backspace, (prefix, zline2, suffix)) when ZExp.is_before_zline(zline2) =>
    switch (split_last(prefix)) {
    | None => Failed
    | Some((prefix, line1)) =>
      switch (line1) {
      | ExpLine(_) => Failed
      | LetLine(_, _, _) => Failed
      | EmptyLine =>
        let zlines = (prefix, zline2, suffix);
        switch (Statics.syn_zlines(ctx, zlines)) {
        | None => Failed
        | Some(ctx) => Succeeded((zlines, ctx, u_gen))
        };
      }
    }
  /* Construction */
  | (
      Construct(SOp(SSpace)),
      (_, CursorL(OnDelim(_, After), LetLine(_, _, _)), _),
    ) =>
    syn_perform_lines(~ci, ctx, MoveRight, edit_state)
  | (Construct(SLine), (prefix, zline, suffix))
      when ZExp.is_before_zline(zline) =>
    let zlines = (prefix @ [EmptyLine], zline, suffix);
    switch (Statics.syn_zlines(ctx, zlines)) {
    | None => Failed
    | Some(ctx) => Succeeded((zlines, ctx, u_gen))
    };
  | (Construct(SLine), (prefix, zline, suffix))
      when ZExp.is_after_zline(zline) =>
    let line = ZExp.erase_zline(zline);
    let zlines = (
      prefix @ [line],
      ZExp.place_before_line(EmptyLine),
      suffix,
    );
    switch (Statics.syn_zlines(ctx, zlines)) {
    | None => Failed
    | Some(ctx) => Succeeded((zlines, ctx, u_gen))
    };
  | (Construct(_), (_, CursorL(_, LetLine(_, _, _)), _)) => Failed
  | (Construct(_), (prefix, CursorL(_, EmptyLine), suffix)) =>
    let (e, u_gen) = UHExp.new_EmptyHole(u_gen);
    let ze = ZExp.place_before_operand(e);
    syn_perform_lines(
      ~ci,
      ctx,
      a,
      ((prefix, ExpLineZ(ze), suffix), u_gen),
    );
  | (Construct(SLet), (prefix, ExpLineZ(ze), suffix))
      when ZExp.is_before_zoperand(ze) =>
    let (zp, u_gen) = ZPat.new_EmptyHole(u_gen);
    let block = UHExp.wrap_in_block(ZExp.erase_zoperand(ze));
    let zline = ZExp.LetLineZP(zp, None, block);
    let zlines = (prefix, zline, suffix);
    switch (Statics.syn_zlines(ctx, zlines)) {
    | None => Failed
    | Some(ctx) => Succeeded((zlines, ctx, u_gen))
    };
  | (Construct(SCase), (prefix, ExpLineZ(ze1), suffix))
      when ZExp.is_before_zoperand(ze1) =>
    let e1 = ZExp.erase_zoperand(ze1);
    let (rule_block, u_gen) =
      /* check if we need to generate concluding expression */
      switch (split_last(suffix)) {
      | None =>
        let (e2, u_gen) = UHExp.new_EmptyHole(u_gen);
        (UHExp.wrap_in_block(e2), u_gen);
      | Some((lines, last_line)) =>
        switch (last_line) {
        | EmptyLine
        | LetLine(_, _, _) =>
          let (e2, u_gen) = UHExp.new_EmptyHole(u_gen);
          (UHExp.Block(suffix, e2), u_gen);
        | ExpLine(e2) => (UHExp.Block(lines, e2), u_gen)
        }
      };
    let (ze, u_gen) =
      switch (e1) {
      | EmptyHole(_) =>
        let (p, u_gen) = UHPat.new_EmptyHole(u_gen);
        let rule = UHExp.Rule(p, rule_block);
        let scrut_zblock = ZExp.BlockZE([], ze1);
        (ZExp.CaseZE(NotInHole, scrut_zblock, [rule], Some(Hole)), u_gen);
      | _ =>
        let (zp, u_gen) = ZPat.new_EmptyHole(u_gen);
        let zrule = ZExp.RuleZP(zp, rule_block);
        let zrules = ZList.singleton(zrule);
        let scrut_block = UHExp.wrap_in_block(e1);
        (ZExp.CaseZR(NotInHole, scrut_block, zrules, Some(Hole)), u_gen);
      };
    let zlines = (prefix, ZExp.ExpLineZ(ze), []);
    switch (Statics.syn_zlines(ctx, zlines)) {
    | None => Failed
    | Some(ctx) => Succeeded((zlines, ctx, u_gen))
    };
  | (
      Construct(SOp(SSpace)),
      (
        prefix,
        ExpLineZ(
          OpSeqZ(
            _,
            CursorE(_, Var(_, InVarHole(Keyword(k), _), _)) as ze0,
            EmptyPrefix(suffix),
          ),
        ),
        suffix,
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    let (e, u_gen) = keyword_suffix_to_exp(suffix, u_gen);
    let ze = ZExp.place_before_operand(e);
    let zlines = (prefix, ZExp.ExpLineZ(ze), suffix);
    syn_perform_lines(~ci, ctx, keyword_action(k), (zlines, u_gen));
  | (
      Construct(SOp(SSpace)),
      (
        prefix,
        ExpLineZ(CursorE(_, Var(_, InVarHole(Keyword(k), _), _)) as ze0),
        suffix,
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    let zlines = (prefix, ZExp.place_before_line(EmptyLine), suffix);
    syn_perform_lines(~ci, ctx, keyword_action(k), (zlines, u_gen));
  /* Zipper Cases */
  | (_, (prefix, zline, suffix)) =>
    switch (Statics.syn_lines(ctx, prefix)) {
    | None => Failed
    | Some(ctx) =>
      switch (syn_perform_line(~ci, ctx, a, (zline, u_gen))) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        switch (ZExp.place_after_lines(prefix)) {
        | None => CursorEscaped(Before)
        | Some((new_prefix, new_zline, _)) =>
          let new_suffix = [ZExp.erase_zline(zline), ...suffix];
          let (new_suffix, ctx, u_gen) =
            Statics.syn_fix_holes_lines(ctx, u_gen, new_suffix);
          let zlines = (new_prefix, new_zline, new_suffix);
          Succeeded((zlines, ctx, u_gen));
        }
      | CursorEscaped(After) =>
        let (suffix, ctx, u_gen) =
          Statics.syn_fix_holes_lines(ctx, u_gen, suffix);
        switch (ZExp.place_before_lines(suffix)) {
        | None => CursorEscaped(After)
        | Some(zorphans) =>
          let (new_prefix_end, ctx, u_gen) =
            Statics.syn_fix_holes_line(ctx, u_gen, ZExp.erase_zline(zline));
          let new_prefix = prefix @ [new_prefix_end];
          let ((_, new_zline, new_suffix), ctx, u_gen) =
            Statics.syn_fix_holes_zlines(ctx, u_gen, zorphans);
          let zlines = (new_prefix, new_zline, new_suffix);
          Succeeded((zlines, ctx, u_gen));
        };
      | Succeeded(((prefix', zline, suffix'), ctx, u_gen)) =>
        let (suffix, ctx, u_gen) =
          Statics.syn_fix_holes_lines(ctx, u_gen, suffix);
        let zlines = (prefix @ prefix', zline, suffix' @ suffix);
        Succeeded((zlines, ctx, u_gen));
      }
    }
  }
and syn_perform_line =
    (
      ~ci: CursorInfo.t,
      ctx: Contexts.t,
      a: t,
      (zline, u_gen): (ZExp.zline, MetaVarGen.t),
    )
    : result((ZExp.zlines, Contexts.t, MetaVarGen.t)) =>
  switch (a, zline) {
  | (
      _,
      CursorL(OnDelim(_, _) | Staging(_), EmptyLine) |
      CursorL(OnText(_), LetLine(_, _, _)) |
      CursorL(_, ExpLine(_)),
    ) =>
    Failed
  | (_, CursorL(cursor, line)) when !ZExp.is_valid_cursor_line(cursor, line) =>
    Failed
  /* Staging */
  | (ShiftUp | ShiftDown | ShiftLeft | ShiftRight, CursorL(_, _)) =>
    // handled at block level
    Failed
  /* Movement */
  | (MoveLeft, _) =>
    zline
    |> ZExp.move_cursor_left_zline
    |> Opt.map_default(~default=CursorEscaped(Before), zline =>
         switch (Statics.syn_line(ctx, zline |> ZExp.erase_zline)) {
         | None => Failed
         | Some(ctx) => Succeeded((([], zline, []), ctx, u_gen))
         }
       )
  | (MoveRight, _) =>
    zline
    |> ZExp.move_cursor_right_zline
    |> Opt.map_default(~default=CursorEscaped(After), zline =>
         switch (Statics.syn_line(ctx, zline |> ZExp.erase_zline)) {
         | None => Failed
         | Some(ctx) => Succeeded((([], zline, []), ctx, u_gen))
         }
       )
  | (MoveTo(_) | MoveToBefore(_) | MoveToPrevHole | MoveToNextHole, _) =>
    /* handled at block or lines level */
    Failed
  /* Backspace & Delete */
  | (Backspace, _) when ZExp.is_before_zline(zline) => CursorEscaped(Before)
  | (Delete, _) when ZExp.is_after_zline(zline) => CursorEscaped(After)
  | (Backspace | Delete, CursorL(Staging(_), _)) =>
    // handled at blocks level
    Failed
  | (Backspace, CursorL(_, EmptyLine)) => CursorEscaped(Before)
  | (Delete, CursorL(_, EmptyLine)) => CursorEscaped(After)
  /* let x :<| Num = 2   ==>   let x| = 2 */
  | (Backspace, CursorL(OnDelim(1, After), LetLine(p, Some(_), block))) =>
    let (block, ty, u_gen) = Statics.syn_fix_holes_block(ctx, u_gen, block);
    let (p, ctx, u_gen) = Statics.ana_fix_holes_pat(ctx, u_gen, p, ty);
    let zp = ZPat.place_after(p);
    let zline = ZExp.LetLineZP(zp, None, block);
    Succeeded((([], zline, []), ctx, u_gen));
  | (Backspace, CursorL(OnDelim(k, After), LetLine(_, _, _) as li)) =>
    switch (Statics.syn_line(ctx, li)) {
    | None => Failed
    | Some(ctx) =>
      Succeeded((([], CursorL(Staging(k), li), []), ctx, u_gen))
    }
  /* let x <|= 2   ==>   let x| = 2 */
  | (Backspace, CursorL(OnDelim(_, Before), LetLine(_, _, _))) =>
    syn_perform_line(~ci, ctx, MoveLeft, (zline, u_gen))
  /* let x =|> 2   ==>   let x = |2 */
  | (Delete, CursorL(OnDelim(_, After), LetLine(_, _, _))) =>
    syn_perform_line(~ci, ctx, MoveRight, (zline, u_gen))
  /* Delete before delimiter == Backspace after delimiter */
  | (Delete, CursorL(OnDelim(k, Before), LetLine(_, _, _) as li)) =>
    syn_perform_line(
      ~ci,
      ctx,
      Backspace,
      (CursorL(OnDelim(k, After), li), u_gen),
    )
  /* Construction */
  | (Construct(_), CursorL(_, _)) =>
    /* handled at lines level */
    Failed
  | (Construct(SAsc), LetLineZP(zp, None, block)) =>
    switch (Statics.syn_block(ctx, block)) {
    | None => Failed
    | Some(ty) =>
      let p = ZPat.erase(zp);
      switch (Statics.Pat.ana(ctx, p, ty)) {
      | None => Failed
      | Some(ctx) =>
        let uty = UHTyp.contract(ty);
        let zty = ZTyp.place_before(uty);
        let zline = ZExp.LetLineZA(p, zty, block);
        Succeeded((([], zline, []), ctx, u_gen));
      };
    }
  | (Construct(SAsc), LetLineZP(zp, Some(uty), block)) =>
    /* just move the cursor over if there is already an ascription */
    let p = ZPat.erase(zp);
    switch (Statics.Pat.ana(ctx, p, UHTyp.expand(uty))) {
    | None => Failed
    | Some(ctx) =>
      let zty = ZTyp.place_before(uty);
      let zline = ZExp.LetLineZA(p, zty, block);
      Succeeded((([], zline, []), ctx, u_gen));
    };
  /* Zipper Cases */
  | (_, ExpLineZ(ze)) =>
    switch (Statics.Exp.syn_operand(ctx, ZExp.erase_zoperand(ze))) {
    | None => Failed
    | Some(ty) =>
      switch (syn_perform_exp(~ci, ctx, a, (ze, ty, u_gen))) {
      | (Failed | CantShift | CursorEscaped(_)) as err => err
      | Succeeded((E(ze), _, u_gen)) =>
        let zline = ZExp.prune_empty_hole_line(ExpLineZ(ze));
        Succeeded((([], zline, []), ctx, u_gen));
      | Succeeded((B(zblock), _, u_gen)) =>
        let zlines = zblock |> ZExp.prune_empty_hole_lines;
        Succeeded((zlines, ctx, u_gen));
      }
    }
  | (_, LetLineZP(zp, ann, block)) =>
    switch (ann) {
    | Some(uty) =>
      let ty = UHTyp.expand(uty);
      switch (Pat.ana_perform(ctx, u_gen, a, zp, ty)) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        syn_perform_line(~ci, ctx, MoveLeft, (zline, u_gen))
      | CursorEscaped(After) =>
        syn_perform_line(~ci, ctx, MoveRight, (zline, u_gen))
      | Succeeded((zp, ctx_after, u_gen)) =>
        let p = ZPat.erase(zp);
        let ctx_block = Statics.ctx_for_let(ctx, p, ty, block);
        let (block, u_gen) =
          Statics.ana_fix_holes_block(ctx_block, u_gen, block, ty);
        let zline = ZExp.LetLineZP(zp, ann, block);
        Succeeded((([], zline, []), ctx_after, u_gen));
      };
    | None =>
      switch (Statics.syn_block(ctx, block)) {
      | None => Failed
      | Some(ty) =>
        switch (Pat.ana_perform(ctx, u_gen, a, zp, ty)) {
        | Failed => Failed
        | CantShift => CantShift
        | CursorEscaped(Before) =>
          syn_perform_line(~ci, ctx, MoveLeft, (zline, u_gen))
        | CursorEscaped(After) =>
          syn_perform_line(~ci, ctx, MoveRight, (zline, u_gen))
        | Succeeded((zp, ctx, u_gen)) =>
          let (block, _, u_gen) =
            Statics.syn_fix_holes_block(ctx, u_gen, block);
          let zline = ZExp.LetLineZP(zp, ann, block);
          Succeeded((([], zline, []), ctx, u_gen));
        }
      }
    }
  | (_, LetLineZA(p, zann, block)) =>
    switch (perform_ty(a, zann)) {
    | Failed => Failed
    | CantShift => CantShift
    | CursorEscaped(Before) =>
      syn_perform_line(~ci, ctx, MoveLeft, (zline, u_gen))
    | CursorEscaped(After) =>
      syn_perform_line(~ci, ctx, MoveRight, (zline, u_gen))
    | Succeeded(zann) =>
      let ty = UHTyp.expand(ZTyp.erase(zann));
      let (p, ctx_after, u_gen) =
        Statics.ana_fix_holes_pat(ctx, u_gen, p, ty);
      let ctx_block = Statics.ctx_for_let(ctx, p, ty, block);
      let (block, u_gen) =
        Statics.ana_fix_holes_block(ctx_block, u_gen, block, ty);
      let zline = ZExp.LetLineZA(p, zann, block);
      Succeeded((([], zline, []), ctx_after, u_gen));
    }
  | (_, LetLineZE(p, ann, zblock)) =>
    switch (ann) {
    | Some(uty) =>
      let ty = UHTyp.expand(uty);
      let ctx_block =
        Statics.ctx_for_let(ctx, p, ty, ZExp.erase_zblock(zblock));
      switch (ana_perform_block(~ci, ctx_block, a, (zblock, u_gen), ty)) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        syn_perform_line(~ci, ctx, MoveLeft, (zline, u_gen))
      | CursorEscaped(After) =>
        syn_perform_line(~ci, ctx, MoveRight, (zline, u_gen))
      | Succeeded((zblock, u_gen)) =>
        switch (Statics.Pat.ana(ctx, p, ty)) {
        | None => Failed
        | Some(ctx) =>
          let zline = ZExp.LetLineZE(p, ann, zblock);
          Succeeded((([], zline, []), ctx, u_gen));
        }
      };
    | None =>
      let block = ZExp.erase_zblock(zblock);
      switch (Statics.syn_block(ctx, block)) {
      | None => Failed
      | Some(ty) =>
        switch (syn_perform_block(~ci, ctx, a, (zblock, ty, u_gen))) {
        | Failed => Failed
        | CantShift => CantShift
        | CursorEscaped(Before) =>
          syn_perform_line(~ci, ctx, MoveLeft, (zline, u_gen))
        | CursorEscaped(After) =>
          syn_perform_line(~ci, ctx, MoveRight, (zline, u_gen))
        | Succeeded((zblock, ty, u_gen)) =>
          let (p, ctx, u_gen) = Statics.ana_fix_holes_pat(ctx, u_gen, p, ty);
          let zline = ZExp.LetLineZE(p, ann, zblock);
          Succeeded((([], zline, []), ctx, u_gen));
        }
      };
    }
  | (UpdateApPalette(_), _) => Failed
  }
and syn_perform_exp =
    (
      ~ci: CursorInfo.t,
      ctx: Contexts.t,
      a: t,
      (ze, ty, u_gen) as edit_state: (ZExp.t, HTyp.t, MetaVarGen.t),
    )
    : result((zexp_or_zblock, HTyp.t, MetaVarGen.t)) =>
  switch (a, ze) {
  | (
      _,
      CursorE(
        OnDelim(_, _),
        Var(_, _, _) | NumLit(_, _) | BoolLit(_, _) | ApPalette(_, _, _, _),
      ) |
      CursorE(
        OnText(_),
        EmptyHole(_) | ListNil(_) | Lam(_, _, _, _) | Inj(_, _, _) |
        Case(_, _, _, _) |
        Parenthesized(_) |
        OpSeq(_, _) |
        ApPalette(_, _, _, _),
      ) |
      CursorE(
        Staging(_),
        EmptyHole(_) | Var(_, _, _) | NumLit(_, _) | BoolLit(_, _) |
        ListNil(_) |
        OpSeq(_, _) |
        ApPalette(_, _, _, _),
      ),
    ) =>
    Failed
  | (_, CursorE(cursor, e)) when !ZExp.is_valid_cursor_operand(cursor, e) =>
    Failed
  /* Staging */
  | (ShiftUp | ShiftDown, CursorE(_, _)) =>
    // handled at block level
    Failed
  | (ShiftLeft | ShiftRight, CursorE(OnText(_) | OnDelim(_, _), _)) =>
    Failed
  | (
      ShiftLeft | ShiftRight,
      CursorE(
        Staging(k),
        (Parenthesized(Block([], body)) | Inj(_, _, Block([], body))) as staged,
      ) |
      OpSeqZ(
        _,
        CursorE(
          Staging(k),
          (Parenthesized(Block([], body)) | Inj(_, _, Block([], body))) as staged,
        ),
        _,
      ),
    ) =>
    let shift_optm =
      switch (k, a) {
      | (0, ShiftLeft) => OpSeqUtil.Exp.shift_optm_from_prefix
      | (0, ShiftRight) => OpSeqUtil.Exp.shift_optm_to_prefix
      | (1, ShiftLeft) => OpSeqUtil.Exp.shift_optm_to_suffix
      | (_one, _shift_right) => OpSeqUtil.Exp.shift_optm_from_suffix
      };
    let surround =
      switch (ze) {
      | OpSeqZ(_, _, surround) => Some(surround)
      | _cursor_e => None
      };
    switch (body |> shift_optm(~surround)) {
    | None => CantShift
    | Some((new_body, new_surround)) =>
      let new_ztm =
        ZExp.CursorE(
          Staging(k),
          switch (staged) {
          | Inj(err, side, _) =>
            Inj(err, side, new_body |> UHExp.wrap_in_block)
          | _parenthesized => Parenthesized(new_body |> UHExp.wrap_in_block)
          },
        );
      let new_ze =
        switch (new_surround) {
        | None => new_ztm
        | Some(surround) => OpSeqUtil.Exp.mk_ZOpSeq(new_ztm, surround)
        };
      let (new_ze, ty, u_gen) =
        Statics.Exp.syn_fix_holes_zoperand(ctx, u_gen, new_ze);
      Succeeded((E(new_ze), ty, u_gen));
    };
  | (
      ShiftLeft | ShiftRight,
      CursorE(
        Staging(_),
        Parenthesized(_) | Inj(_) | Lam(_, _, _, _) | Case(_, _, _, _),
      ),
    ) =>
    // line shifting is handled at block level
    CantShift
  /* Movement */
  | (MoveTo(path), _) =>
    let e = ZExp.erase_zoperand(ze);
    switch (CursorPath.follow_exp(path, e)) {
    | None => Failed
    | Some(ze) => Succeeded((E(ze), ty, u_gen))
    };
  | (MoveToBefore(steps), _) =>
    let e = ZExp.erase_zoperand(ze);
    switch (CursorPath.follow_exp_and_place_before(steps, e)) {
    | None => Failed
    | Some(ze) => Succeeded((E(ze), ty, u_gen))
    };
  | (MoveToPrevHole, _) =>
    let holes = CursorPath.holes_ze(ze, []);
    switch (CursorPath.prev_hole_steps(holes)) {
    | None => Failed
    | Some(path) =>
      syn_perform_exp(~ci, ctx, MoveTo(path), (ze, ty, u_gen))
    };
  | (MoveToNextHole, _) =>
    let holes = CursorPath.holes_ze(ze, []);
    switch (CursorPath.next_hole_steps(holes)) {
    | None => Failed
    | Some(path) =>
      syn_perform_exp(~ci, ctx, MoveTo(path), (ze, ty, u_gen))
    };
  | (MoveLeft, _) =>
    ZExp.move_cursor_left_zoperand(ze)
    |> Opt.map_default(~default=CursorEscaped(Before), ze =>
         Succeeded((ZExp.E(ze), ty, u_gen))
       )
  | (MoveRight, _) =>
    ZExp.move_cursor_right_zoperand(ze)
    |> Opt.map_default(~default=CursorEscaped(After), ze =>
         Succeeded((ZExp.E(ze), ty, u_gen))
       )
  /* Backspace & Deletion */
  | (Backspace, _) when ZExp.is_before_zoperand(ze) => CursorEscaped(Before)
  | (Delete, _) when ZExp.is_after_zoperand(ze) => CursorEscaped(After)
  | (Backspace, CursorE(_, EmptyHole(_) as e)) =>
    ZExp.is_after_zoperand(ze)
      ? Succeeded((E(ZExp.place_before_operand(e)), Hole, u_gen))
      : CursorEscaped(Before)
  | (Delete, CursorE(_, EmptyHole(_) as e)) =>
    ZExp.is_before_zoperand(ze)
      ? Succeeded((E(ZExp.place_after_operand(e)), Hole, u_gen))
      : CursorEscaped(After)
  | (
      Backspace | Delete,
      CursorE(OnText(_), Var(_, _, _) | NumLit(_, _) | BoolLit(_, _)),
    )
  | (Backspace | Delete, CursorE(OnDelim(_, _), ListNil(_))) =>
    let (ze, u_gen) = ZExp.new_EmptyHole(u_gen);
    Succeeded((E(ze), Hole, u_gen));
  /* ( _ <|)   ==>   ( _| ) */
  /* ... + [k-1] <|+ [k] + ...   ==>   ... + [k-1]| + [k] + ... */
  | (
      Backspace,
      CursorE(
        OnDelim(_, Before),
        Lam(_, _, _, _) | Inj(_, _, _) | Case(_, _, _, _) | Parenthesized(_) |
        OpSeq(_, _),
      ),
    ) =>
    syn_perform_exp(~ci, ctx, MoveLeft, (ze, ty, u_gen))
  /* (|> _ )   ==>   ( |_ ) */
  /* ... + [k-1] +|> [k] + ...   ==>   ... + [k-1] + |[k] + ... */
  | (
      Delete,
      CursorE(
        OnDelim(_, After),
        Lam(_, _, _, _) | Inj(_, _, _) | Case(_, _, _, _) | Parenthesized(_) |
        OpSeq(_, _),
      ),
    ) =>
    syn_perform_exp(~ci, ctx, MoveRight, (ze, ty, u_gen))
  /* Delete before delimiter == Backspace after delimiter */
  | (
      Delete,
      CursorE(
        OnDelim(k, Before),
        (
          Lam(_, _, _, _) | Inj(_, _, _) | Case(_, _, _, _) |
          Parenthesized(_) |
          OpSeq(_, _)
        ) as e,
      ),
    ) =>
    syn_perform_exp(
      ~ci,
      ctx,
      Backspace,
      (CursorE(OnDelim(k, After), e), ty, u_gen),
    )
  /* \x :<| Num . x + 1   ==>   \x| . x + 1 */
  | (Backspace, CursorE(OnDelim(1, After), Lam(_, p, Some(_), block))) =>
    let (p, ctx, u_gen) = Statics.ana_fix_holes_pat(ctx, u_gen, p, Hole);
    let (block, ty2, u_gen) = Statics.syn_fix_holes_block(ctx, u_gen, block);
    let ze = ZExp.LamZP(NotInHole, ZPat.place_after(p), None, block);
    Succeeded((E(ze), Arrow(Hole, ty2), u_gen));
  | (
      Backspace,
      CursorE(
        OnDelim(k, After),
        (
          Lam(_, _, _, _) | Inj(_, _, _) | Case(_, _, _, _) |
          Parenthesized(_)
        ) as e,
      ),
    ) =>
    Succeeded((E(CursorE(Staging(k), e)), ty, u_gen))
  | (
      Backspace | Delete,
      CursorE(Staging(k), Parenthesized(Block(lines, e) as body)),
    ) =>
    let (result, u_gen) =
      switch (ci.frame, lines, e, e |> UHExp.bidelimited) {
      | (ExpFrame(_, None, _), _, _, _)
      | (_, _, OpSeq(_, _), _)
      | (ExpFrame(_, Some(_), _), [], _, true) => (
          body
          |> (
            switch (k) {
            | 0 => ZExp.place_before_block
            | _one => ZExp.place_after_block
            }
          ),
          u_gen,
        )
      | (_exp_frame_some, _, _, _) =>
        let (hole, u_gen) = u_gen |> ZExp.new_EmptyHole;
        (hole |> ZExp.wrap_in_block, u_gen);
      };
    Succeeded((B(result), ty, u_gen));
  | (Backspace | Delete, CursorE(Staging(k), Case(_, scrut, _, _))) =>
    let result =
      scrut
      |> (
        switch (k) {
        | 0 => ZExp.place_before_block
        | _one => ZExp.place_after_block
        }
      );
    let (result, ty, u_gen) =
      Statics.Exp.syn_fix_holes_z(ctx, u_gen, result);
    Succeeded((B(result), ty, u_gen));
  | (Backspace | Delete, CursorE(Staging(k), Inj(_, _, body))) =>
    let result =
      body
      |> (
        switch (k) {
        | 0 => ZExp.place_before_block
        | _one => ZExp.place_after_block
        }
      );
    let (result, ty, u_gen) =
      Statics.Exp.syn_fix_holes_z(ctx, u_gen, result);
    Succeeded((B(result), ty, u_gen));
  | (Backspace | Delete, CursorE(Staging(k), Lam(_, _, _, body))) =>
    let result =
      body
      |> (
        switch (k) {
        | 0 => ZExp.place_before_block
        | _one => ZExp.place_after_block
        }
      );
    let (result, ty, u_gen) =
      Statics.Exp.syn_fix_holes_z(ctx, u_gen, result);
    Succeeded((B(result), ty, u_gen));
  /* TODO consider deletion of type ascription on case */
  | (Backspace, CaseZR(_, _, (_, CursorR(OnDelim(_, Before), _), _), _)) =>
    syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
  | (Delete, CaseZR(_, _, (_, CursorR(OnDelim(_, After), _), _), _)) =>
    syn_perform_exp(~ci, ctx, MoveRight, edit_state)
  // Delete before delim == Backspace after delim
  | (
      Delete,
      CaseZR(
        err,
        scrut,
        (prefix, CursorR(OnDelim(k, Before), rule), suffix),
        ann,
      ),
    ) =>
    syn_perform_exp(
      ~ci=ci |> CursorInfo.update_position(OnDelim(k, After)),
      ctx,
      Backspace,
      (
        ZExp.CaseZR(
          err,
          scrut,
          (prefix, CursorR(OnDelim(k, After), rule), suffix),
          ann,
        ),
        ty,
        u_gen,
      ),
    )
  | (
      Backspace,
      CaseZR(
        err,
        scrut,
        (prefix, CursorR(OnDelim(k, After), rule), suffix),
        ann,
      ),
    ) =>
    Succeeded((
      E(
        CaseZR(
          err,
          scrut,
          (prefix, CursorR(Staging(k), rule), suffix),
          ann,
        ),
      ),
      ty,
      u_gen,
    ))
  | (
      Backspace | Delete,
      CaseZR(_, scrut, (prefix, CursorR(Staging(_), _), suffix), ann),
    ) =>
    switch (suffix, prefix |> split_last) {
    | ([], None) =>
      let (zrule, u_gen) = ZExp.empty_zrule(u_gen);
      Succeeded((
        E(CaseZR(NotInHole, scrut, ([], zrule, []), ann)),
        ty,
        u_gen,
      ));
    | ([first, ...rest], _) =>
      let zrule = ZExp.place_before_rule(first);
      let ze = ZExp.CaseZR(NotInHole, scrut, (prefix, zrule, rest), ann);
      Succeeded((E(ze), ty, u_gen));
    | (_, Some((prefix_prefix, prefix_last))) =>
      let zrule = ZExp.place_after_rule(prefix_last);
      let ze =
        ZExp.CaseZR(NotInHole, scrut, (prefix_prefix, zrule, suffix), ann);
      Succeeded((E(ze), ty, u_gen));
    }
  /* ... + [k-1] +<| [k] + ... */
  | (Backspace, CursorE(OnDelim(k, After), OpSeq(_, seq))) =>
    /* validity check at top of switch statement ensures
     * that op between [k-1] and [k] is not Space */
    switch (Seq.split(k - 1, seq), Seq.split(k, seq)) {
    /* invalid cursor position */
    | (None, _)
    | (_, None) => Failed
    /* ... + [k-1] +<| _ + ... */
    | (_, Some((EmptyHole(_), surround))) =>
      switch (surround) {
      /* invalid */
      | EmptyPrefix(_) => Failed
      /* ... + [k-1] +<| _   ==>   ... + [k-1]| */
      | EmptySuffix(prefix) =>
        let ze: ZExp.t =
          switch (prefix) {
          | OperandPrefix(e, _) => ZExp.place_after_operand(e)
          | SeqPrefix(seq, _) =>
            let skel = Associator.associate_exp(seq);
            ZExp.place_after_operand(OpSeq(skel, seq));
          };
        let (ze, ty, u_gen) =
          Statics.Exp.syn_fix_holes_zoperand(ctx, u_gen, ze);
        Succeeded((E(ze), ty, u_gen));
      /* ... + [k-1] +<| _ + ...   ==>   ... + [k-1]| + ... */
      | BothNonEmpty(prefix, suffix) =>
        let (ze0: ZExp.t, surround: ZExp.opseq_surround) =
          switch (prefix) {
          | OperandPrefix(e, _) => (
              ZExp.place_after_operand(e),
              EmptyPrefix(suffix),
            )
          | SeqPrefix(ExpOpExp(e1, op, e2), _) => (
              ZExp.place_after_operand(e2),
              BothNonEmpty(OperandPrefix(e1, op), suffix),
            )
          | SeqPrefix(SeqOpExp(seq, op, e), _) => (
              ZExp.place_after_operand(e),
              BothNonEmpty(SeqPrefix(seq, op), suffix),
            )
          };
        let skel =
          Associator.associate_exp(
            Seq.t_of_operand_and_surround(
              ZExp.erase_zoperand(ze0),
              surround,
            ),
          );
        let ze = ZExp.OpSeqZ(skel, ze0, surround);
        let (ze, ty, u_gen) =
          Statics.Exp.syn_fix_holes_zoperand(ctx, u_gen, ze);
        Succeeded((E(ze), ty, u_gen));
      }
    /* ... + _ +<| [k] + ... */
    | (Some((EmptyHole(_), surround)), _) =>
      switch (surround) {
      /* invalid */
      | EmptySuffix(_) => Failed
      /* _ +<| [k] + ...   ==>   |[k] + ... */
      | EmptyPrefix(suffix) =>
        let ze: ZExp.t =
          switch (suffix) {
          | OperandSuffix(_, e) => ZExp.place_before_operand(e)
          | SeqSuffix(_, seq) =>
            let skel = Associator.associate_exp(seq);
            ZExp.place_before_operand(OpSeq(skel, seq));
          };
        let (ze, ty, u_gen) =
          Statics.Exp.syn_fix_holes_zoperand(ctx, u_gen, ze);
        Succeeded((E(ze), ty, u_gen));
      /* ... + [k-2] + _ +<| [k] + ...   ==>   ... + [k-2] +| [k] + ... */
      | BothNonEmpty(prefix, suffix) =>
        let seq =
          switch (suffix) {
          | OperandSuffix(_, e) => Seq.t_of_prefix_and_last(prefix, e)
          | SeqSuffix(_, seq) => Seq.t_of_prefix_and_seq(prefix, seq)
          };
        let skel = Associator.associate_exp(seq);
        let ze = ZExp.CursorE(OnDelim(k - 1, After), OpSeq(skel, seq));
        let (ze, ty, u_gen) =
          Statics.Exp.syn_fix_holes_zoperand(ctx, u_gen, ze);
        Succeeded((E(ze), ty, u_gen));
      }
    /* ... + [k-1] +<| [k] + ...   ==>   ... + [k-1]| [k] + ... */
    | (Some((e0, surround)), _) =>
      switch (Seq.replace_following_op(surround, UHExp.Space)) {
      | None => Failed /* invalid */
      | Some(surround) =>
        let (ze, ty, u_gen) =
          make_and_syn_OpSeqZ(
            ctx,
            u_gen,
            ZExp.place_after_operand(e0),
            surround,
          );
        Succeeded((E(ze), ty, u_gen));
      }
    }
  /* ... + [k-1]  <|_ + [k+1] + ...  ==>   ... + [k-1]| + [k+1] + ... */
  | (
      Backspace,
      OpSeqZ(
        _,
        CursorE(_, EmptyHole(_)) as ze0,
        (
          EmptySuffix(OperandPrefix(_, Space) | SeqPrefix(_, Space)) |
          BothNonEmpty(OperandPrefix(_, Space) | SeqPrefix(_, Space), _)
        ) as surround,
      ),
    )
      when ZExp.is_before_zoperand(ze0) =>
    switch (surround) {
    | EmptyPrefix(_) => CursorEscaped(Before) /* should never happen */
    | EmptySuffix(prefix) =>
      let e: UHExp.t =
        switch (prefix) {
        | OperandPrefix(e1, _space) => e1
        | SeqPrefix(seq, _space) =>
          let skel = Associator.associate_exp(seq);
          OpSeq(skel, seq);
        };
      let (ze, ty, u_gen) =
        Statics.Exp.syn_fix_holes_zoperand(
          ctx,
          u_gen,
          ZExp.place_after_operand(e),
        );
      Succeeded((E(ze), ty, u_gen));
    | BothNonEmpty(prefix, suffix) =>
      switch (prefix) {
      | OperandPrefix(e1, _space) =>
        let ze1 = ZExp.place_after_operand(e1);
        let seq = Seq.t_of_first_and_suffix(e1, suffix);
        let skel = Associator.associate_exp(seq);
        let ze = ZExp.OpSeqZ(skel, ze1, EmptyPrefix(suffix));
        let (ze, ty, u_gen) =
          Statics.Exp.syn_fix_holes_zoperand(ctx, u_gen, ze);
        Succeeded((E(ze), ty, u_gen));
      | SeqPrefix(seq, _space) =>
        let (prefix: ZExp.prefix, e0) =
          switch (seq) {
          | ExpOpExp(e1, op, e2) => (OperandPrefix(e1, op), e2)
          | SeqOpExp(seq, op, e1) => (SeqPrefix(seq, op), e1)
          };
        let ze0 = ZExp.place_after_operand(e0);
        let surround = Seq.BothNonEmpty(prefix, suffix);
        let seq = Seq.t_of_operand_and_surround(e0, surround);
        let skel = Associator.associate_exp(seq);
        let ze = ZExp.OpSeqZ(skel, ze0, surround);
        let (ze, ty, u_gen) =
          Statics.Exp.syn_fix_holes_zoperand(ctx, u_gen, ze);
        Succeeded((E(ze), ty, u_gen));
      }
    }
  /* ... + [k-1] + _|>  [k+1] + ...  ==>   ... + [k-1] + |[k+1] + ... */
  | (
      Delete,
      OpSeqZ(
        _,
        CursorE(_, EmptyHole(_)) as ze0,
        (
          EmptyPrefix(OperandSuffix(Space, _) | SeqSuffix(Space, _)) |
          BothNonEmpty(_, OperandSuffix(Space, _) | SeqSuffix(Space, _))
        ) as surround,
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    switch (surround) {
    | EmptySuffix(_) => CursorEscaped(After) /* should never happen */
    | EmptyPrefix(suffix) =>
      let e =
        switch (suffix) {
        | OperandSuffix(_space, e1) => e1
        | SeqSuffix(_space, seq) =>
          let skel = Associator.associate_exp(seq);
          OpSeq(skel, seq);
        };
      let (ze, ty, u_gen) =
        Statics.Exp.syn_fix_holes_zoperand(
          ctx,
          u_gen,
          ZExp.place_before_operand(e),
        );
      Succeeded((E(ze), ty, u_gen));
    | BothNonEmpty(prefix, suffix) =>
      switch (suffix) {
      | OperandSuffix(_space, e1) =>
        let ze1 = ZExp.place_before_operand(e1);
        let seq = Seq.t_of_prefix_and_last(prefix, e1);
        let skel = Associator.associate_exp(seq);
        let ze = ZExp.OpSeqZ(skel, ze1, EmptySuffix(prefix));
        let (ze, ty, u_gen) =
          Statics.Exp.syn_fix_holes_zoperand(ctx, u_gen, ze);
        Succeeded((E(ze), ty, u_gen));
      | SeqSuffix(_space, seq) =>
        let (e0, suffix: ZExp.suffix) =
          switch (seq) {
          | ExpOpExp(e1, op, e2) => (e1, OperandSuffix(op, e2))
          | SeqOpExp(seq, op, e1) => (e1, SeqSuffix(op, seq))
          };
        let ze0 = ZExp.place_before_operand(e0);
        let surround = Seq.BothNonEmpty(prefix, suffix);
        let seq = Seq.t_of_operand_and_surround(e0, surround);
        let skel = Associator.associate_exp(seq);
        let ze = ZExp.OpSeqZ(skel, ze0, surround);
        let (ze, ty, u_gen) =
          Statics.Exp.syn_fix_holes_zoperand(ctx, u_gen, ze);
        Succeeded((E(ze), ty, u_gen));
      }
    }
  /* Construction */
  | (Construct(SLine), CursorE(Staging(k), e)) =>
    let (new_ze, ty, u_gen) =
      Statics.Exp.syn_fix_holes_zoperand(
        ctx,
        u_gen,
        CursorE(OnDelim(k, After), e),
      );
    Succeeded((E(new_ze), ty, u_gen));
  | (Construct(_), CursorE(Staging(_), _)) => Failed
  | (
      Construct(SLine),
      CaseZR(err, scrut, (prefix, CursorR(Staging(k), rule), suffix), ann),
    ) =>
    let (new_ze, ty, u_gen) =
      Statics.Exp.syn_fix_holes_zoperand(
        ctx,
        u_gen,
        CaseZR(
          err,
          scrut,
          (prefix, CursorR(OnDelim(k, After), rule), suffix),
          ann,
        ),
      );
    Succeeded((E(new_ze), ty, u_gen));
  | (
      Construct(SOp(SSpace)),
      CursorE(OnDelim(_, After), _) |
      CaseZR(_, _, (_, CursorR(OnDelim(_, After), _), _), _),
    )
      when !ZExp.is_after_zoperand(ze) =>
    syn_perform_exp(~ci, ctx, MoveRight, edit_state)
  | (Construct(_) as a, CursorE(OnDelim(_, side), _))
      when !ZExp.is_before_zoperand(ze) && !ZExp.is_after_zoperand(ze) =>
    let move_then_perform = move_action =>
      switch (syn_perform_exp(~ci, ctx, move_action, edit_state)) {
      | Failed
      | CantShift
      | CursorEscaped(_)
      | Succeeded((B(_), _, _)) => assert(false)
      | Succeeded((E(ze), ty, u_gen)) =>
        CursorInfo.syn_cursor_info(
          ~frame=ci |> CursorInfo.force_get_exp_frame,
          ctx,
          ze,
        )
        |> Opt.map_default(~default=Failed, ci =>
             syn_perform_exp(~ci, ctx, a, (ze, ty, u_gen))
           )
      };
    switch (side) {
    | Before => move_then_perform(MoveLeft)
    | After => move_then_perform(MoveRight)
    };
  | (Construct(SLine), CursorE(_, _))
  | (Construct(SLet), CursorE(_, _)) =>
    /* handled at block or line level */
    Failed
  | (
      Construct(SLine),
      CaseZR(_, e1, (prefix, RuleZP(zp, re), suffix), ann),
    )
      when ZPat.is_before(zp) =>
    let (zrule, u_gen) = ZExp.empty_zrule(u_gen);
    let prev_rule = UHExp.Rule(ZPat.erase(zp), re);
    let suffix = [prev_rule, ...suffix];
    let ze = ZExp.CaseZR(NotInHole, e1, (prefix, zrule, suffix), ann);
    Succeeded((E(ze), ty, u_gen));
  | (
      Construct(SLine),
      CaseZR(_, e1, (prefix, RuleZE(_, ze) as zrule, suffix), ann),
    )
      when ZExp.is_after_zblock(ze) =>
    let prev_rule = ZExp.erase_zrule(zrule);
    let (zrule, u_gen) = ZExp.empty_zrule(u_gen);
    let prefix = prefix @ [prev_rule];
    let ze = ZExp.CaseZR(NotInHole, e1, (prefix, zrule, suffix), ann);
    Succeeded((E(ze), ty, u_gen));
  | (
      Construct(SLine),
      CaseZR(_, e1, (prefix, RuleZP(zp, _) as zrule, suffix), ann),
    )
      when ZPat.is_after(zp) =>
    let prev_rule = ZExp.erase_zrule(zrule);
    let (zrule, u_gen) = ZExp.empty_zrule(u_gen);
    let prefix = prefix @ [prev_rule];
    let ze = ZExp.CaseZR(NotInHole, e1, (prefix, zrule, suffix), ann);
    Succeeded((E(ze), ty, u_gen));
  | (Construct(SCase), ze1) when ZExp.is_before_zoperand(ze1) =>
    let e1 = ZExp.erase_zoperand(ze1);
    let (ze, u_gen) =
      switch (e1) {
      | EmptyHole(_) =>
        let (rule, u_gen) = UHExp.empty_rule(u_gen);
        (
          ZExp.CaseZE(
            NotInHole,
            ZExp.wrap_in_block(ze1),
            [rule],
            Some(Hole),
          ),
          u_gen,
        );
      | _ =>
        let (zrule, u_gen) = ZExp.empty_zrule(u_gen);
        let zrules = ZList.singleton(zrule);
        (
          ZExp.CaseZR(
            NotInHole,
            UHExp.wrap_in_block(e1),
            zrules,
            Some(Hole),
          ),
          u_gen,
        );
      };
    Succeeded((E(ze), Hole, u_gen));
  | (Construct(SCase), CursorE(_, _)) => Failed
  | (Construct(SParenthesized), CursorE(_, _)) =>
    let zblock = ZExp.BlockZE([], ze);
    Succeeded((E(ParenthesizedZ(zblock)), ty, u_gen));
  | (Construct(SAsc), LamZP(err, zp, None, e1)) =>
    let ze = ZExp.LamZA(err, ZPat.erase(zp), ZTyp.place_before(Hole), e1);
    Succeeded((E(ze), ty, u_gen));
  | (Construct(SAsc), LamZP(err, zp, Some(uty1), e1)) =>
    /* just move the cursor over if there is already an ascription */
    let ze = ZExp.LamZA(err, ZPat.erase(zp), ZTyp.place_before(uty1), e1);
    Succeeded((E(ze), ty, u_gen));
  | (Construct(SAsc), CursorE(_, Case(_, e1, rules, Some(uty)))) =>
    /* just move the cursor over if there is already an ascription */
    let ze = ZExp.CaseZA(NotInHole, e1, rules, ZTyp.place_before(uty));
    Succeeded((E(ze), ty, u_gen));
  | (Construct(SAsc), CursorE(_, _)) => Failed
  | (Construct(SVar(x, cursor)), CursorE(_, EmptyHole(_)))
  | (Construct(SVar(x, cursor)), CursorE(_, Var(_, _, _)))
  | (Construct(SVar(x, cursor)), CursorE(_, NumLit(_, _)))
  | (Construct(SVar(x, cursor)), CursorE(_, BoolLit(_, _))) =>
    if (String.equal(x, "true")) {
      Succeeded((
        E(CursorE(cursor, BoolLit(NotInHole, true))),
        Bool,
        u_gen,
      ));
    } else if (String.equal(x, "false")) {
      Succeeded((
        E(CursorE(cursor, BoolLit(NotInHole, false))),
        Bool,
        u_gen,
      ));
    } else if (Var.is_let(x)) {
      let (u, u_gen) = MetaVarGen.next(u_gen);
      Succeeded((
        E(CursorE(cursor, Var(NotInHole, InVarHole(Keyword(Let), u), x))),
        Hole,
        u_gen,
      ));
    } else if (Var.is_case(x)) {
      let (u, u_gen) = MetaVarGen.next(u_gen);
      Succeeded((
        E(
          CursorE(cursor, Var(NotInHole, InVarHole(Keyword(Case), u), x)),
        ),
        Hole,
        u_gen,
      ));
    } else {
      check_valid(
        x,
        {
          let gamma = Contexts.gamma(ctx);
          switch (VarMap.lookup(gamma, x)) {
          | Some(xty) =>
            Succeeded((
              ZExp.E(ZExp.CursorE(cursor, Var(NotInHole, NotInVarHole, x))),
              xty,
              u_gen,
            ))
          | None =>
            let (u, u_gen) = MetaVarGen.next(u_gen);
            Succeeded((
              ZExp.E(
                ZExp.CursorE(cursor, Var(NotInHole, InVarHole(Free, u), x)),
              ),
              HTyp.Hole,
              u_gen,
            ));
          };
        },
      );
    }
  | (Construct(SVar(_, _)), CursorE(_, _)) => Failed
  | (Construct(SLam), CursorE(_, _) as ze1) =>
    let e1 = ZExp.erase_zoperand(ze1);
    let (zp, u_gen) = ZPat.new_EmptyHole(u_gen);
    let block = UHExp.wrap_in_block(e1);
    let ze = ZExp.LamZP(NotInHole, zp, Some(Hole), block);
    let ty' = HTyp.Arrow(Hole, ty);
    Succeeded((E(ze), ty', u_gen));
  | (Construct(SNumLit(n, cursor)), CursorE(_, EmptyHole(_)))
  | (Construct(SNumLit(n, cursor)), CursorE(_, NumLit(_, _)))
  | (Construct(SNumLit(n, cursor)), CursorE(_, BoolLit(_, _)))
  | (Construct(SNumLit(n, cursor)), CursorE(_, Var(_, _, _))) =>
    Succeeded((E(CursorE(cursor, NumLit(NotInHole, n))), Num, u_gen))
  | (Construct(SNumLit(_, _)), CursorE(_, _)) => Failed
  | (Construct(SInj(side)), CursorE(_, _)) =>
    let zblock = ZExp.BlockZE([], ze);
    let ze' = ZExp.InjZ(NotInHole, side, zblock);
    let ty' =
      switch (side) {
      | L => HTyp.Sum(ty, Hole)
      | R => HTyp.Sum(Hole, ty)
      };
    Succeeded((E(ze'), ty', u_gen));
  | (Construct(SListNil), CursorE(_, EmptyHole(_))) =>
    let ze = ZExp.place_after_operand(ListNil(NotInHole));
    let ty = HTyp.List(Hole);
    Succeeded((E(ze), ty, u_gen));
  | (Construct(SListNil), CursorE(_, _)) => Failed
  | (
      Construct(SOp(SSpace)),
      OpSeqZ(_, CursorE(OnDelim(_, After), _) as ze0, _),
    )
      when !ZExp.is_after_zoperand(ze0) =>
    syn_perform_exp(~ci, ctx, MoveRight, edit_state)
  | (Construct(SOp(os)), OpSeqZ(_, ze0, surround))
      when ZExp.is_after_zoperand(ze0) =>
    switch (exp_op_of(os)) {
    | None => Failed
    | Some(op) =>
      let (ze, ty, u_gen) =
        abs_perform_Construct_SOp_After_surround(
          UHExp.new_EmptyHole,
          make_and_syn_OpSeq,
          make_and_syn_OpSeqZ,
          UHExp.is_Space,
          UHExp.Space,
          ZExp.place_before_operand,
          ctx,
          u_gen,
          ZExp.erase_zoperand(ze0),
          op,
          surround,
        );
      Succeeded((E(ze), ty, u_gen));
    }
  | (Construct(SOp(os)), OpSeqZ(_, ze0, surround))
      when ZExp.is_before_zoperand(ze0) =>
    switch (exp_op_of(os)) {
    | None => Failed
    | Some(op) =>
      let (ze, ty, u_gen) =
        abs_perform_Construct_SOp_Before_surround(
          UHExp.new_EmptyHole,
          make_and_syn_OpSeq,
          make_and_syn_OpSeqZ,
          UHExp.is_Space,
          UHExp.Space,
          ZExp.place_before_operand,
          ctx,
          u_gen,
          ze0 |> ZExp.erase_zoperand,
          op,
          surround,
        );
      Succeeded((E(ze), ty, u_gen));
    }
  | (Construct(SOp(os)), CursorE(_, _)) =>
    switch (exp_op_of(os)) {
    | None => Failed
    | Some(op) =>
      if (ZExp.is_before_zoperand(ze)) {
        let (ze, ty, u_gen) =
          abs_perform_Construct_SOp_Before(
            UHExp.bidelimit,
            UHExp.new_EmptyHole,
            make_and_syn_OpSeq,
            make_and_syn_OpSeqZ,
            UHExp.is_Space,
            ZExp.place_before_operand,
            ctx,
            u_gen,
            ZExp.erase_zoperand(ze),
            op,
          );
        Succeeded((E(ze), ty, u_gen));
      } else if (ZExp.is_after_zoperand(ze)) {
        let (ze, ty, u_gen) =
          abs_perform_Construct_SOp_After(
            UHExp.bidelimit,
            UHExp.new_EmptyHole,
            make_and_syn_OpSeq,
            make_and_syn_OpSeqZ,
            UHExp.is_Space,
            ZExp.place_before_operand,
            ctx,
            u_gen,
            ZExp.erase_zoperand(ze),
            op,
          );
        Succeeded((E(ze), ty, u_gen));
      } else {
        Failed;
      }
    }
  | (Construct(SApPalette(name)), CursorE(_, EmptyHole(_))) =>
    let palette_ctx = Contexts.palette_ctx(ctx);
    switch (PaletteCtx.lookup(palette_ctx, name)) {
    | None => Failed
    | Some(palette_defn) =>
      let init_model_cmd = palette_defn.init_model;
      let (init_model, init_splice_info, u_gen) =
        SpliceGenMonad.exec(init_model_cmd, SpliceInfo.empty, u_gen);
      switch (Statics.ana_splice_map(ctx, init_splice_info.splice_map)) {
      | None => Failed
      | Some(splice_ctx) =>
        let expansion_ty = palette_defn.expansion_ty;
        let expand = palette_defn.expand;
        let expansion = expand(init_model);
        switch (Statics.ana_block(splice_ctx, expansion, expansion_ty)) {
        | None => Failed
        | Some(_) =>
          Succeeded((
            E(
              ZExp.place_before_operand(
                ApPalette(NotInHole, name, init_model, init_splice_info),
              ),
            ),
            expansion_ty,
            u_gen,
          ))
        };
      };
    };
  | (Construct(SApPalette(_)), CursorE(_, _)) => Failed
  /* TODO
     | (UpdateApPalette(_), CursorE(_, ApPalette(_, _name, _, _hole_data))) =>
        let (_, palette_ctx) = ctx;
        switch (PaletteCtx.lookup(palette_ctx, name)) {
        | Some(palette_defn) =>
          let (q, u_gen') = UHExp.HoleRefs.exec(monad, hole_data, u_gen);
          let (serialized_model, hole_data') = q;
          let expansion_ty = UHExp.PaletteDefinition.expansion_ty(palette_defn);
          let expansion =
            (UHExp.PaletteDefinition.to_exp(palette_defn))(serialized_model);
          let (_, hole_map') = hole_data';
          let expansion_ctx =
            UHExp.PaletteHoleData.extend_ctx_with_hole_map(ctx, hole_map');
          switch (Statics.ana(expansion_ctx, expansion, expansion_ty)) {
          | Some(_) =>
            Succeeded((
              CursorE(
                After,
                Tm(
                  NotInHole,
                  ApPalette(name, serialized_model, hole_data'),
                ),
              ),
              expansion_ty,
              u_gen,
            ))
          | None => Failed
          };
        | None => Failed
        }; */
  | (UpdateApPalette(_), CursorE(_, _)) => Failed
  /* Zipper Cases */
  | (_, ParenthesizedZ(zblock)) =>
    switch (syn_perform_block(~ci, ctx, a, (zblock, ty, u_gen))) {
    | Failed => Failed
    | CantShift => CantShift
    | CursorEscaped(Before) =>
      syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
    | CursorEscaped(After) =>
      syn_perform_exp(~ci, ctx, MoveRight, edit_state)
    | Succeeded((ze1', ty', u_gen')) =>
      Succeeded((E(ParenthesizedZ(ze1')), ty', u_gen'))
    }
  | (_, LamZP(_, zp, ann, block)) =>
    let ty1 =
      switch (ann) {
      | Some(uty1) => UHTyp.expand(uty1)
      | None => HTyp.Hole
      };
    switch (Pat.ana_perform(ctx, u_gen, a, zp, ty1)) {
    | Failed => Failed
    | CantShift => CantShift
    | CursorEscaped(Before) =>
      syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
    | CursorEscaped(After) =>
      syn_perform_exp(~ci, ctx, MoveRight, edit_state)
    | Succeeded((zp, ctx, u_gen)) =>
      let (block, ty2, u_gen) =
        Statics.syn_fix_holes_block(ctx, u_gen, block);
      let ty = HTyp.Arrow(ty1, ty2);
      let ze = ZExp.LamZP(NotInHole, zp, ann, block);
      Succeeded((E(ze), ty, u_gen));
    };
  | (_, LamZA(_, p, zann, block)) =>
    switch (perform_ty(a, zann)) {
    | Failed => Failed
    | CantShift => CantShift
    | CursorEscaped(Before) =>
      syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
    | CursorEscaped(After) =>
      syn_perform_exp(~ci, ctx, MoveRight, edit_state)
    | Succeeded(zann) =>
      let ty1 = UHTyp.expand(ZTyp.erase(zann));
      let (p, ctx, u_gen) = Statics.ana_fix_holes_pat(ctx, u_gen, p, ty1);
      let (block, ty2, u_gen) =
        Statics.syn_fix_holes_block(ctx, u_gen, block);
      let ze = ZExp.LamZA(NotInHole, p, zann, block);
      Succeeded((E(ze), Arrow(ty1, ty2), u_gen));
    }
  | (_, LamZE(_, p, ann, zblock)) =>
    switch (HTyp.matched_arrow(ty)) {
    | None => Failed
    | Some((_, ty2)) =>
      let ty1 =
        switch (ann) {
        | Some(uty1) => UHTyp.expand(uty1)
        | None => HTyp.Hole
        };
      switch (Statics.Pat.ana(ctx, p, ty1)) {
      | None => Failed
      | Some(ctx_body) =>
        switch (syn_perform_block(~ci, ctx_body, a, (zblock, ty2, u_gen))) {
        | Failed => Failed
        | CantShift => CantShift
        | CursorEscaped(Before) =>
          syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
        | CursorEscaped(After) =>
          syn_perform_exp(~ci, ctx, MoveRight, edit_state)
        | Succeeded((zblock, ty2, u_gen)) =>
          let ze = ZExp.LamZE(NotInHole, p, ann, zblock);
          Succeeded((E(ze), Arrow(ty1, ty2), u_gen));
        }
      };
    }
  | (_, InjZ(_, side, zblock)) =>
    switch (ty) {
    | Sum(ty1, ty2) =>
      let ty_side = InjSide.pick(side, ty1, ty2);
      switch (syn_perform_block(~ci, ctx, a, (zblock, ty_side, u_gen))) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
      | CursorEscaped(After) =>
        syn_perform_exp(~ci, ctx, MoveRight, edit_state)
      | Succeeded((zblock, ty_side', u_gen)) =>
        let ty' =
          switch (side) {
          | L => HTyp.Sum(ty_side', ty2)
          | R => HTyp.Sum(ty1, ty_side')
          };
        Succeeded((E(InjZ(NotInHole, side, zblock)), ty', u_gen));
      };
    | _ => Failed /* should never happen */
    }
  | (_, OpSeqZ(_, ze0, surround)) =>
    let i = Seq.surround_prefix_length(surround);
    switch (ZExp.erase_zoperand(ze)) {
    | OpSeq(skel, seq) =>
      switch (Statics.syn_skel(ctx, skel, seq, Some(i))) {
      | Some((_, Some(mode))) =>
        switch (mode) {
        | AnalyzedAgainst(ty0) =>
          switch (ana_perform_exp(~ci, ctx, a, (ze0, u_gen), ty0)) {
          | Failed => Failed
          | CantShift => CantShift
          | CursorEscaped(Before) =>
            syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
          | CursorEscaped(After) =>
            syn_perform_exp(~ci, ctx, MoveRight, edit_state)
          | Succeeded((ze_zb, u_gen)) =>
            let ze0 =
              switch (ze_zb) {
              | E(ze) => ZExp.bidelimit(ze)
              | B(zblock) =>
                switch (zblock) {
                | BlockZL(_, _)
                | BlockZE([_, ..._], _) => ParenthesizedZ(zblock)
                | BlockZE([], ze) => ze
                }
              };
            let (ze0, surround) = OpSeqUtil.Exp.resurround(ze0, surround);
            let (ze, ty, u_gen) =
              make_and_syn_OpSeqZ(ctx, u_gen, ze0, surround);
            Succeeded((E(ze), ty, u_gen));
          }
        | Synthesized(ty0) =>
          switch (syn_perform_exp(~ci, ctx, a, (ze0, ty0, u_gen))) {
          | Failed => Failed
          | CantShift => CantShift
          | CursorEscaped(Before) =>
            syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
          | CursorEscaped(After) =>
            syn_perform_exp(~ci, ctx, MoveRight, edit_state)
          | Succeeded((ze_or_zblock, _, u_gen)) =>
            let ze0 =
              switch (ze_or_zblock) {
              | E(ze) => ZExp.bidelimit(ze)
              | B(zblock) =>
                switch (zblock) {
                | BlockZL(_, _)
                | BlockZE([_, ..._], _) => ParenthesizedZ(zblock)
                | BlockZE([], ze) => ze
                }
              };
            let (ze0, surround) = OpSeqUtil.Exp.resurround(ze0, surround);
            let (ze, ty, u_gen) =
              make_and_syn_OpSeqZ(ctx, u_gen, ze0, surround);
            Succeeded((E(ze), ty, u_gen));
          }
        }
      | Some(_) => Failed /* should never happen */
      | None => Failed /* should never happen */
      }
    | _ => Failed /* should never happen */
    };
  | (_, ApPaletteZ(_, _name, _serialized_model, _z_hole_data)) => Failed
  /* TODO let (next_lbl, z_nat_map) = z_hole_data;
     let (rest_map, z_data) = z_nat_map;
     let (cell_lbl, cell_data) = z_data;
     let (cell_ty, cell_ze) = cell_data;
     switch (ana_perform_exp(~ci, ctx, a, (cell_ze, u_gen), cell_ty)) {
     | Failed => Failed
     | CantShift => CantShift
     | Succeeded((cell_ze', u_gen')) =>
       let z_hole_data' = (
         next_lbl,
         (rest_map, (cell_lbl, (cell_ty, cell_ze'))),
       );
       Succeeded((
         ApPaletteZ(NotInHole, name, serialized_model, z_hole_data'),
         ty,
         u_gen',
       ));
     }; */
  | (_, CaseZE(_, _, _, None))
  | (_, CaseZR(_, _, _, None)) => Failed
  | (_, CaseZE(_, zblock, rules, Some(uty) as ann)) =>
    switch (Statics.syn_block(ctx, ZExp.erase_zblock(zblock))) {
    | None => Failed
    | Some(ty1) =>
      switch (syn_perform_block(~ci, ctx, a, (zblock, ty1, u_gen))) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
      | CursorEscaped(After) =>
        syn_perform_exp(~ci, ctx, MoveRight, edit_state)
      | Succeeded((zblock, ty1, u_gen)) =>
        let ty = UHTyp.expand(uty);
        let (rules, u_gen) =
          Statics.ana_fix_holes_rules(ctx, u_gen, rules, ty1, ty);
        let ze = ZExp.CaseZE(NotInHole, zblock, rules, ann);
        Succeeded((E(ze), ty, u_gen));
      }
    }
  | (_, CaseZR(_, block, zrules, Some(uty) as ann)) =>
    switch (Statics.syn_block(ctx, block)) {
    | None => Failed
    | Some(ty1) =>
      switch (ZList.prj_z(zrules)) {
      | CursorR(_, _) => Failed /* handled in earlier case */
      | RuleZP(zp, clause) =>
        switch (Pat.ana_perform(ctx, u_gen, a, zp, ty1)) {
        | Failed => Failed
        | CantShift => CantShift
        | CursorEscaped(Before) =>
          syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
        | CursorEscaped(After) =>
          syn_perform_exp(~ci, ctx, MoveRight, edit_state)
        | Succeeded((zp, ctx, u_gen)) =>
          let ty = UHTyp.expand(uty);
          let (clause, u_gen) =
            Statics.ana_fix_holes_block(ctx, u_gen, clause, ty);
          let zrule = ZExp.RuleZP(zp, clause);
          let ze =
            ZExp.CaseZR(
              NotInHole,
              block,
              ZList.replace_z(zrules, zrule),
              ann,
            );
          Succeeded((E(ze), ty, u_gen));
        }
      | RuleZE(p, zclause) =>
        switch (Statics.Pat.ana(ctx, p, ty1)) {
        | None => Failed
        | Some(ctx) =>
          let ty = UHTyp.expand(uty);
          switch (ana_perform_block(~ci, ctx, a, (zclause, u_gen), ty)) {
          | Failed => Failed
          | CantShift => CantShift
          | CursorEscaped(Before) =>
            syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
          | CursorEscaped(After) =>
            syn_perform_exp(~ci, ctx, MoveRight, edit_state)
          | Succeeded((zclause, u_gen)) =>
            let zrule = ZExp.RuleZE(p, zclause);
            let ze =
              ZExp.CaseZR(
                NotInHole,
                block,
                ZList.replace_z(zrules, zrule),
                ann,
              );
            Succeeded((E(ze), ty, u_gen));
          };
        }
      }
    }
  | (_, CaseZA(_, block, rules, zann)) =>
    switch (Statics.syn_block(ctx, block)) {
    | None => Failed
    | Some(ty1) =>
      switch (perform_ty(a, zann)) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        syn_perform_exp(~ci, ctx, MoveLeft, edit_state)
      | CursorEscaped(After) =>
        syn_perform_exp(~ci, ctx, MoveRight, edit_state)
      | Succeeded(zann) =>
        let ty = UHTyp.expand(ZTyp.erase(zann));
        let (rules, u_gen) =
          Statics.ana_fix_holes_rules(ctx, u_gen, rules, ty1, ty);
        let ze = ZExp.CaseZA(NotInHole, block, rules, zann);
        Succeeded((E(ze), ty, u_gen));
      }
    }
  /* Invalid actions at expression level */
  | (Construct(SNum), _)
  | (Construct(SBool), _)
  | (Construct(SList), _)
  | (Construct(SWild), _) => Failed
  }
and ana_perform_block =
    (
      ~ci: CursorInfo.t,
      ctx: Contexts.t,
      a: t,
      (zblock, u_gen): (ZExp.zblock, MetaVarGen.t),
      ty: HTyp.t,
    )
    : result((ZExp.zblock, MetaVarGen.t)) =>
  switch (a, zblock) {
  /* Staging */
  | (
      ShiftUp | ShiftDown,
      BlockZL(
        (prefix, CursorL(Staging(3), LetLine(p, ann, def)), suffix),
        e,
      ),
    ) =>
    let shift_line =
      switch (a) {
      | ShiftUp => UHExp.shift_line_to_suffix_block
      | _ => UHExp.shift_line_from_suffix_block(~is_node_terminal=false)
      };
    switch (def |> shift_line(~u_gen, Some(Block(suffix, e)))) {
    | None => CantShift
    | Some((_, None, _)) => assert(false)
    | Some((new_def, Some(Block(new_suffix, new_e)), u_gen)) =>
      let new_zblock =
        ZExp.BlockZL(
          (
            prefix,
            CursorL(Staging(3), LetLine(p, ann, new_def)),
            new_suffix,
          ),
          new_e,
        );
      Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, new_zblock, ty));
    };
  | (
      ShiftUp | ShiftDown | ShiftLeft | ShiftRight,
      BlockZL((_, CursorL(Staging(_), LetLine(_, _, _)), _), _),
    ) =>
    CantShift
  | (
      ShiftLeft | ShiftRight,
      BlockZL((_, CursorL(Staging(_), EmptyLine | ExpLine(_)), _), _),
    ) =>
    Failed
  | (
      ShiftUp | ShiftDown,
      BlockZL(
        (
          prefix,
          ExpLineZ(
            CursorE(
              Staging(0),
              (
                Parenthesized(block) | Inj(_, _, block) |
                Case(_, block, _, _)
              ) as e_line,
            ),
          ),
          suffix,
        ),
        e,
      ),
    ) =>
    let shift_line =
      switch (a) {
      | ShiftLeft => UHExp.shift_line_from_prefix
      | _ => UHExp.shift_line_to_prefix
      };
    switch (block |> shift_line(~u_gen, prefix)) {
    | None => CantShift
    | Some((new_prefix, new_block, u_gen)) =>
      let new_e_line =
        switch (e_line) {
        | Inj(err, side, _) => UHExp.Inj(err, side, new_block)
        | Case(err, _, rules, ann) => Case(err, new_block, rules, ann)
        | _ => Parenthesized(new_block)
        };
      let new_zblock =
        ZExp.BlockZL(
          (new_prefix, ExpLineZ(CursorE(Staging(0), new_e_line)), suffix),
          e,
        );
      Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, new_zblock, ty));
    };
  | (
      ShiftUp | ShiftDown,
      BlockZL(
        (
          prefix,
          ExpLineZ(
            CursorE(
              Staging(1) as cursor,
              (Parenthesized(block) | Inj(_, _, block)) as e_line,
            ),
          ),
          suffix,
        ),
        e,
      ),
    ) =>
    let shift_line =
      switch (a) {
      | ShiftUp => UHExp.shift_line_to_suffix_block
      | _ => UHExp.shift_line_from_suffix_block(~is_node_terminal=true)
      };
    switch (block |> shift_line(~u_gen, Some(Block(suffix, e)))) {
    | None => CantShift
    | Some((new_block, None, u_gen)) =>
      let new_conclusion =
        switch (e_line) {
        | Inj(err, side, _) => UHExp.Inj(err, side, new_block)
        | _ => Parenthesized(new_block)
        };
      let new_zblock = ZExp.BlockZE(prefix, CursorE(cursor, new_conclusion));
      Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, new_zblock, ty));
    | Some((new_block, Some(Block(new_suffix, new_e)), u_gen)) =>
      let new_e_line =
        switch (e_line) {
        | Inj(err, side, _) => UHExp.Inj(err, side, new_block)
        | _ => Parenthesized(new_block)
        };
      let new_zblock =
        ZExp.BlockZL(
          (prefix, ExpLineZ(CursorE(Staging(1), new_e_line)), new_suffix),
          new_e,
        );
      Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, new_zblock, ty));
    };
  | (
      ShiftUp | ShiftDown,
      BlockZL(
        (
          prefix,
          ExpLineZ(
            CursorE(Staging(1) as cursor, Case(err, scrut, rules, None)),
          ),
          suffix,
        ),
        e,
      ),
    ) =>
    switch (rules |> split_last) {
    | None => Failed // shouldn't ever see empty rule list
    | Some((leading_rules, Rule(last_p, last_clause))) =>
      let shift_line =
        switch (a) {
        | ShiftUp => UHExp.shift_line_to_suffix_block
        | _ => UHExp.shift_line_from_suffix_block(~is_node_terminal=true)
        };
      switch (last_clause |> shift_line(~u_gen, Some(Block(suffix, e)))) {
      | None => CantShift
      | Some((new_last_clause, None, u_gen)) =>
        let new_conclusion =
          UHExp.Case(
            err,
            scrut,
            leading_rules @ [Rule(last_p, new_last_clause)],
            None,
          );
        let new_zblock =
          ZExp.BlockZE(prefix, CursorE(cursor, new_conclusion));
        Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, new_zblock, ty));
      | Some((new_last_clause, Some(Block(new_suffix, new_e)), u_gen)) =>
        let new_zblock =
          ZExp.BlockZL(
            (
              prefix,
              ExpLineZ(
                CursorE(
                  Staging(1),
                  Case(
                    err,
                    scrut,
                    leading_rules @ [Rule(last_p, new_last_clause)],
                    None,
                  ),
                ),
              ),
              new_suffix,
            ),
            new_e,
          );
        Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, new_zblock, ty));
      };
    }
  | (
      ShiftRight,
      BlockZE(
        leading,
        CursorE(
          Staging(0) as cursor,
          (Parenthesized(Block([], body)) | Inj(_, _, Block([], body))) as staged,
        ),
      ),
    ) =>
    switch (body |> OpSeqUtil.Exp.shift_optm_to_prefix(~surround=None)) {
    | None => CantShift
    | Some((new_body, new_surround)) =>
      let new_ztm =
        ZExp.CursorE(
          cursor,
          switch (staged) {
          | Inj(err, side, _) =>
            Inj(err, side, new_body |> UHExp.wrap_in_block)
          | _parenthesized => Parenthesized(new_body |> UHExp.wrap_in_block)
          },
        );
      let new_ze =
        switch (new_surround) {
        | None => new_ztm
        | Some(surround) => OpSeqUtil.Exp.mk_ZOpSeq(new_ztm, surround)
        };
      let new_zblock = ZExp.BlockZE(leading, new_ze);
      Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, new_zblock, ty));
    }
  | (
      ShiftUp,
      BlockZL(
        (
          leading_prefix,
          ExpLineZ(
            OpSeqZ(
              _,
              CursorE(Staging(1) as cursor, Parenthesized(Block([], body))),
              EmptySuffix(prefix),
            ),
          ),
          leading_suffix,
        ),
        conclusion,
      ),
    ) =>
    // skip over remaining left shifts, then apply ShiftUp to result
    let skipped_body = OpSeqUtil.Exp.prepend(prefix, body);
    let skipped_zblock =
      ZExp.BlockZL(
        (
          leading_prefix,
          ExpLineZ(
            CursorE(cursor, Parenthesized(Block([], skipped_body))),
          ),
          leading_suffix,
        ),
        conclusion,
      );
    ana_perform_block(
      ~ci,
      ctx,
      ShiftUp,
      Statics.ana_fix_holes_zblock(ctx, u_gen, skipped_zblock, ty),
      ty,
    );
  | (
      ShiftDown,
      BlockZL(
        (
          leading_prefix,
          ExpLineZ(
            OpSeqZ(
              _,
              CursorE(Staging(1) as cursor, Parenthesized(Block([], body))),
              EmptyPrefix(suffix),
            ),
          ),
          leading_suffix,
        ),
        conclusion,
      ),
    ) =>
    // skip over remaining right shifts, then apply ShiftDown to result
    let skipped_body = OpSeqUtil.Exp.append(body, suffix);
    let skipped_zblock =
      ZExp.BlockZL(
        (
          leading_prefix,
          ExpLineZ(
            CursorE(cursor, Parenthesized(Block([], skipped_body))),
          ),
          leading_suffix,
        ),
        conclusion,
      );
    ana_perform_block(
      ~ci,
      ctx,
      ShiftDown,
      Statics.ana_fix_holes_zblock(ctx, u_gen, skipped_zblock, ty),
      ty,
    );
  | (
      ShiftUp,
      BlockZE(
        leading,
        OpSeqZ(
          _,
          CursorE(Staging(1) as cursor, Parenthesized(Block([], body))),
          EmptySuffix(prefix),
        ),
      ),
    ) =>
    // skip over remaining left shifts, then apply ShiftUp to result
    let skipped_body = OpSeqUtil.Exp.prepend(prefix, body);
    let skipped_zblock =
      ZExp.BlockZE(
        leading,
        CursorE(cursor, Parenthesized(Block([], skipped_body))),
      );
    ana_perform_block(
      ~ci,
      ctx,
      ShiftUp,
      Statics.ana_fix_holes_zblock(ctx, u_gen, skipped_zblock, ty),
      ty,
    );
  | (
      ShiftUp,
      BlockZE(leading, CursorE(Staging(1) as cursor, Parenthesized(body))),
    ) =>
    switch (body |> UHExp.shift_line_to_suffix_block(~u_gen, None)) {
    | None => CantShift
    | Some((_, None, _)) => assert(false)
    | Some((
        new_body,
        Some(Block(new_suffix_leading, new_suffix_conclusion)),
        u_gen,
      )) =>
      let new_zblock =
        ZExp.BlockZL(
          (
            leading,
            ExpLineZ(CursorE(cursor, Parenthesized(new_body))),
            new_suffix_leading,
          ),
          new_suffix_conclusion,
        );
      Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, new_zblock, ty));
    }
  | (
      ShiftLeft,
      BlockZE(
        leading,
        CursorE(
          Staging(1) as cursor,
          (Parenthesized(Block([], body)) | Inj(_, _, Block([], body))) as staged,
        ),
      ),
    ) =>
    switch (body |> OpSeqUtil.Exp.shift_optm_to_suffix(~surround=None)) {
    | None => CantShift
    | Some((new_body, new_surround)) =>
      let new_ztm =
        ZExp.CursorE(
          cursor,
          switch (staged) {
          | Inj(err, side, _) =>
            Inj(err, side, new_body |> UHExp.wrap_in_block)
          | _parenthesized => Parenthesized(new_body |> UHExp.wrap_in_block)
          },
        );
      let new_ze =
        switch (new_surround) {
        | None => new_ztm
        | Some(surround) => OpSeqUtil.Exp.mk_ZOpSeq(new_ztm, surround)
        };
      let new_zblock = ZExp.BlockZE(leading, new_ze);
      Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, new_zblock, ty));
    }
  | (
      ShiftUp | ShiftDown,
      BlockZE(
        leading,
        CursorE(
          Staging(0) as cursor,
          (Parenthesized(block) | Inj(_, _, block) | Case(_, block, _, _)) as conclusion,
        ),
      ),
    ) =>
    let shift_line =
      switch (a) {
      | ShiftUp => UHExp.shift_line_from_prefix
      | _ => UHExp.shift_line_to_prefix
      };
    switch (block |> shift_line(~u_gen, leading)) {
    | None => CantShift
    | Some((new_leading, new_block, u_gen)) =>
      let new_conclusion =
        switch (conclusion) {
        | Inj(err, side, _) => UHExp.Inj(err, side, new_block)
        | Case(err, _, rules, ann) => Case(err, new_block, rules, ann)
        | _ => Parenthesized(new_block)
        };
      let new_zblock =
        ZExp.BlockZE(new_leading, CursorE(cursor, new_conclusion));
      Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, new_zblock, ty));
    };
  /* Movement */
  | (MoveTo(path), _) =>
    let block = ZExp.erase_zblock(zblock);
    switch (CursorPath.follow_block(path, block)) {
    | None => Failed
    | Some(zblock) => Succeeded((zblock, u_gen))
    };
  | (MoveToBefore(steps), _) =>
    let block = ZExp.erase_zblock(zblock);
    switch (CursorPath.follow_block_and_place_before(steps, block)) {
    | None => Failed
    | Some(zblock) => Succeeded((zblock, u_gen))
    };
  | (MoveToPrevHole, _) =>
    switch (CursorPath.Exp.prev_hole_steps_z(zblock)) {
    | None => Failed
    | Some(path) =>
      ana_perform_block(~ci, ctx, MoveTo(path), (zblock, u_gen), ty)
    }
  | (MoveToNextHole, _) =>
    switch (CursorPath.Exp.next_hole_steps_z(zblock)) {
    | None => Failed
    | Some(path) =>
      ana_perform_block(~ci, ctx, MoveTo(path), (zblock, u_gen), ty)
    }
  | (MoveLeft, _) =>
    switch (ZExp.move_cursor_left_zblock(zblock)) {
    | None => CursorEscaped(Before)
    | Some(zblock) => Succeeded((zblock, u_gen))
    }
  | (MoveRight, _) =>
    switch (ZExp.move_cursor_right_zblock(zblock)) {
    | None => CursorEscaped(After)
    | Some(zblock) => Succeeded((zblock, u_gen))
    }
  /* Backspace & Delete */
  | (Backspace, _) when ZExp.is_before_zblock(zblock) =>
    CursorEscaped(Before)
  | (Delete, _) when ZExp.is_after_zblock(zblock) => CursorEscaped(After)
  | (Delete, BlockZL((prefix, CursorL(_, EmptyLine), []), e)) =>
    let ze = ZExp.place_before_operand(e);
    let zblock = ZExp.BlockZE(prefix, ze);
    Succeeded((zblock, u_gen));
  | (Backspace, BlockZE(leading, zconclusion))
      when ZExp.is_before_zoperand(zconclusion) =>
    switch (leading |> split_last, zconclusion |> ZExp.erase_zoperand) {
    | (None, _) => CursorEscaped(Before)
    | (Some((leading_prefix, EmptyLine)), _) =>
      Succeeded((BlockZE(leading_prefix, zconclusion), u_gen))
    | (Some((leading_prefix, ExpLine(e))), EmptyHole(_)) =>
      let new_zconclusion = ZExp.place_after_operand(e);
      Succeeded(
        Statics.ana_fix_holes_zblock(
          ctx,
          u_gen,
          BlockZE(leading_prefix, new_zconclusion),
          ty,
        ),
      );
    | (Some((leading_prefix, leading_last)), conclusion) =>
      let zleading_last = ZExp.place_after_line(leading_last);
      let zblock =
        ZExp.BlockZL((leading_prefix, zleading_last, []), conclusion);
      Succeeded((zblock, u_gen));
    }
  | (Delete, BlockZL((prefix, ExpLineZ(ze), []), EmptyHole(_)))
      when ZExp.is_after_zoperand(ze) =>
    let zblock = ZExp.BlockZE(prefix, ze);
    Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, zblock, ty));
  | (
      Backspace | Delete,
      BlockZL((prefix, CursorL(Staging(k), _), suffix), conclusion),
    ) =>
    let new_zblock: option(ZExp.zblock) =
      switch (ci |> CursorInfo.preserved_child_term_of_node, suffix) {
      | (Some((_, Type(_) | Pattern(_))), _) => None
      | (None, []) =>
        // If deleted line is followed by an empty hole,
        // then they are on the same visual line. Don't bother
        // leaving behind an empty line, instead let the
        // the empty hole take the deleted line's place.
        switch (conclusion) {
        | EmptyHole(_) =>
          Some(BlockZE(prefix, conclusion |> ZExp.place_before_operand))
        | _ =>
          Some(
            BlockZL(
              (prefix, ZExp.place_before_line(EmptyLine), []),
              conclusion,
            ),
          )
        }
      | (None, [_, ..._]) =>
        Some(
          BlockZL(
            (prefix, ZExp.place_before_line(EmptyLine), suffix),
            conclusion,
          ),
        )
      | (Some((_, Expression(block))), _) =>
        let place_cursor =
          // here we're depending on the fact that
          // only let lines can preserve children
          switch (k) {
          | 0
          | 1
          | 2 => ZExp.place_before_block
          | _three => ZExp.place_after_block
          };
        let (inner_prefix, zline, inner_suffix) =
          block |> place_cursor |> ZExp.prune_empty_hole_lines;
        Some(
          BlockZL(
            (prefix @ inner_prefix, zline, inner_suffix @ suffix),
            conclusion,
          ),
        );
      };
    new_zblock
    |> Opt.map_default(~default=Failed, zblock =>
         Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, zblock, ty))
       );
  /* Construction */
  | (
      Construct(SLine),
      BlockZL((prefix, CursorL(Staging(k), line), suffix), e),
    ) =>
    Succeeded(
      Statics.ana_fix_holes_zblock(
        ctx,
        u_gen,
        BlockZL((prefix, CursorL(OnDelim(k, After), line), suffix), e),
        ty,
      ),
    )
  | (
      Construct(SLine),
      BlockZL((prefix, ExpLineZ(CursorE(Staging(k), e_line)), suffix), e),
    ) =>
    Succeeded(
      Statics.ana_fix_holes_zblock(
        ctx,
        u_gen,
        BlockZL(
          (prefix, ExpLineZ(CursorE(OnDelim(k, After), e_line)), suffix),
          e,
        ),
        ty,
      ),
    )
  | (Construct(_), BlockZL((_, CursorL(Staging(_), _), _), _)) => Failed
  | (Construct(SLine), BlockZE(lines, ze)) when ZExp.is_before_zoperand(ze) =>
    let zblock = ZExp.BlockZE(lines @ [EmptyLine], ze);
    Succeeded((zblock, u_gen));
  | (Construct(SLine), BlockZE(lines, ze)) when ZExp.is_after_zoperand(ze) =>
    switch (Statics.syn_lines(ctx, lines)) {
    | None => Failed
    | Some(ctx) =>
      let (e, _, u_gen) =
        Statics.syn_fix_holes_exp(ctx, u_gen, ZExp.erase_zoperand(ze));
      let line = UHExp.prune_empty_hole_line(ExpLine(e));
      let (zhole, u_gen) = ZExp.new_EmptyHole(u_gen);
      let zblock = ZExp.BlockZE(lines @ [line], zhole);
      Succeeded((zblock, u_gen));
    }
  | (Construct(SLet), BlockZE(lines, ze1))
      when ZExp.is_before_zoperand(ze1) =>
    let (zp, u_gen) = ZPat.new_EmptyHole(u_gen);
    let block = UHExp.wrap_in_block(ZExp.erase_zoperand(ze1));
    let zline = ZExp.LetLineZP(zp, None, block);
    let zlines = (lines, zline, []);
    let (e2, u_gen) = UHExp.new_EmptyHole(u_gen);
    let zblock = ZExp.BlockZL(zlines, e2);
    Succeeded((zblock, u_gen));
  | (
      Construct(SCase),
      BlockZL(
        (prefix, (CursorL(_, EmptyLine) | ExpLineZ(_)) as zline, suffix),
        e2,
      ),
    )
      when ZExp.is_before_zline(zline) =>
    let (e1, u_gen) =
      switch (zline) {
      | ExpLineZ(ze1) => (ZExp.erase_zoperand(ze1), u_gen)
      | _ =>
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (EmptyHole(u), u_gen);
      };
    let clause = UHExp.Block(suffix, e2);
    let (ze, u_gen) =
      switch (e1) {
      | EmptyHole(_) =>
        let (p, u_gen) = UHPat.new_EmptyHole(u_gen);
        let rule = UHExp.Rule(p, clause);
        (
          ZExp.CaseZE(
            NotInHole,
            ZExp.BlockZE([], ZExp.place_before_operand(e1)),
            [rule],
            None,
          ),
          u_gen,
        );
      | _ =>
        let (zp, u_gen) = ZPat.new_EmptyHole(u_gen);
        let zrule = ZExp.RuleZP(zp, clause);
        let zrules = ZList.singleton(zrule);
        (
          ZExp.CaseZR(NotInHole, UHExp.wrap_in_block(e1), zrules, None),
          u_gen,
        );
      };
    let zblock = ZExp.BlockZE(prefix, ze);
    Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, zblock, ty));
  | (
      Construct(SOp(SSpace)),
      BlockZL(
        (
          prefix,
          ExpLineZ(
            OpSeqZ(
              _,
              CursorE(_, Var(_, InVarHole(Keyword(k), _), _)) as ze0,
              EmptyPrefix(suffix),
            ),
          ),
          suffix,
        ),
        e2,
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    let (e, u_gen) = keyword_suffix_to_exp(suffix, u_gen);
    let ze = ZExp.place_before_operand(e);
    let zlines = (prefix, ZExp.ExpLineZ(ze), suffix);
    let zblock = ZExp.BlockZL(zlines, e2);
    ana_perform_block(~ci, ctx, keyword_action(k), (zblock, u_gen), ty);
  | (
      Construct(SOp(SSpace)),
      BlockZL(
        (
          prefix,
          ExpLineZ(CursorE(_, Var(_, InVarHole(Keyword(k), _), _)) as ze0),
          suffix,
        ),
        e2,
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    let zlines = (prefix, ZExp.place_before_line(EmptyLine), suffix);
    let zblock = ZExp.BlockZL(zlines, e2);
    ana_perform_block(~ci, ctx, keyword_action(k), (zblock, u_gen), ty);
  | (
      Construct(SOp(SSpace)),
      BlockZE(
        lines,
        OpSeqZ(
          _,
          CursorE(_, Var(_, InVarHole(Keyword(k), _), _)) as ze0,
          EmptyPrefix(suffix),
        ),
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    let (e, u_gen) = keyword_suffix_to_exp(suffix, u_gen);
    let ze = ZExp.place_before_operand(e);
    let zblock = ZExp.BlockZE(lines, ze);
    ana_perform_block(~ci, ctx, keyword_action(k), (zblock, u_gen), ty);
  | (
      Construct(SOp(SSpace)),
      BlockZE(
        lines,
        CursorE(_, Var(_, InVarHole(Keyword(k), _), _)) as ze0,
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    let (ze, u_gen) = ZExp.new_EmptyHole(u_gen);
    let zblock = ZExp.BlockZE(lines, ze);
    ana_perform_block(~ci, ctx, keyword_action(k), (zblock, u_gen), ty);
  /* Zipper Cases */
  | (
      Backspace | Delete | Construct(_) | UpdateApPalette(_) | ShiftLeft |
      ShiftRight |
      ShiftUp |
      ShiftDown,
      BlockZL(zlines, e),
    ) =>
    switch (syn_perform_lines(~ci, ctx, a, (zlines, u_gen))) {
    | Failed => Failed
    | CantShift => CantShift
    | CursorEscaped(Before) => CursorEscaped(Before)
    | CursorEscaped(After) =>
      Succeeded((
        BlockZE(ZExp.erase_zlines(zlines), ZExp.place_before_operand(e)),
        u_gen,
      ))
    | Succeeded((zlines, _, u_gen)) =>
      let zblock = ZExp.BlockZL(zlines, e);
      Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, zblock, ty));
    }
  | (
      Backspace | Delete | Construct(_) | UpdateApPalette(_) | ShiftLeft |
      ShiftRight |
      ShiftUp |
      ShiftDown,
      BlockZE(lines, ze),
    ) =>
    switch (Statics.syn_lines(ctx, lines)) {
    | None => Failed
    | Some(ctx1) =>
      switch (ana_perform_exp(~ci, ctx1, a, (ze, u_gen), ty)) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        switch (ZExp.place_after_lines(lines)) {
        | None => CursorEscaped(Before)
        | Some(zlines) =>
          Succeeded((BlockZL(zlines, ZExp.erase_zoperand(ze)), u_gen))
        }
      | CursorEscaped(After) => CursorEscaped(After)
      | Succeeded((E(ze), u_gen)) => Succeeded((BlockZE(lines, ze), u_gen))
      | Succeeded((B(zblock), u_gen)) =>
        switch (zblock) {
        | BlockZL((prefix, zline, suffix), e) =>
          let zblock = ZExp.BlockZL((lines @ prefix, zline, suffix), e);
          Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, zblock, ty));
        | BlockZE(ls, ze) =>
          let zblock = ZExp.BlockZE(lines @ ls, ze);
          Succeeded(Statics.ana_fix_holes_zblock(ctx, u_gen, zblock, ty));
        }
      }
    }
  }
and ana_perform_exp =
    (
      ~ci: CursorInfo.t,
      ctx: Contexts.t,
      a: t,
      (ze, u_gen) as edit_state: (ZExp.t, MetaVarGen.t),
      ty: HTyp.t,
    )
    : result((zexp_or_zblock, MetaVarGen.t)) =>
  switch (a, ze) {
  | (
      _,
      CursorE(
        OnDelim(_, _),
        Var(_, _, _) | NumLit(_, _) | BoolLit(_, _) | ApPalette(_, _, _, _),
      ) |
      CursorE(
        OnText(_),
        EmptyHole(_) | ListNil(_) | Lam(_, _, _, _) | Inj(_, _, _) |
        Case(_, _, _, _) |
        Parenthesized(_) |
        OpSeq(_, _) |
        ApPalette(_, _, _, _),
      ) |
      CursorE(
        Staging(_),
        EmptyHole(_) | Var(_, _, _) | NumLit(_, _) | BoolLit(_, _) |
        ListNil(_) |
        OpSeq(_, _) |
        ApPalette(_, _, _, _),
      ),
    ) =>
    Failed
  | (_, CursorE(cursor, e)) when !ZExp.is_valid_cursor_operand(cursor, e) =>
    Failed
  | (_, _) when ZExp.is_inconsistent(ze) =>
    let err = ze |> ZExp.get_err_status_operand;
    let ze' = ZExp.set_err_status_operand(NotInHole, ze);
    let e' = ZExp.erase_zoperand(ze');
    switch (Statics.Exp.syn_operand(ctx, e')) {
    | None => Failed
    | Some(ty1) =>
      switch (syn_perform_exp(~ci, ctx, a, (ze', ty1, u_gen))) {
      | (Failed | CursorEscaped(_) | CantShift) as result => result
      | Succeeded((ze', ty1', u_gen')) =>
        if (HTyp.consistent(ty1', ty)) {
          Succeeded((ze', u_gen'));
        } else {
          Succeeded((set_err_status_zexp_or_zblock(err, ze'), u_gen'));
        }
      }
    };
  /* Staging */
  | (ShiftUp | ShiftDown, CursorE(_, _)) =>
    // handled at block level
    Failed
  | (ShiftLeft | ShiftRight, CursorE(OnText(_) | OnDelim(_, _), _)) =>
    Failed
  | (
      ShiftLeft | ShiftRight,
      CursorE(
        Staging(k),
        (Parenthesized(Block([], body)) | Inj(_, _, Block([], body))) as staged,
      ) |
      OpSeqZ(
        _,
        CursorE(
          Staging(k),
          (Parenthesized(Block([], body)) | Inj(_, _, Block([], body))) as staged,
        ),
        _,
      ),
    ) =>
    let shift_optm =
      switch (k, a) {
      | (0, ShiftLeft) => OpSeqUtil.Exp.shift_optm_from_prefix
      | (0, ShiftRight) => OpSeqUtil.Exp.shift_optm_to_prefix
      | (1, ShiftLeft) => OpSeqUtil.Exp.shift_optm_to_suffix
      | (_one, _shift_right) => OpSeqUtil.Exp.shift_optm_from_suffix
      };
    let surround =
      switch (ze) {
      | OpSeqZ(_, _, surround) => Some(surround)
      | _cursor_e => None
      };
    switch (body |> shift_optm(~surround)) {
    | None => CantShift
    | Some((new_body, new_surround)) =>
      let new_ztm =
        ZExp.CursorE(
          Staging(k),
          switch (staged) {
          | Inj(err, side, _) =>
            Inj(err, side, new_body |> UHExp.wrap_in_block)
          | _parenthesized => Parenthesized(new_body |> UHExp.wrap_in_block)
          },
        );
      let new_ze =
        switch (new_surround) {
        | None => new_ztm
        | Some(surround) => OpSeqUtil.Exp.mk_ZOpSeq(new_ztm, surround)
        };
      let (new_ze, u_gen) =
        Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, new_ze, ty);
      Succeeded((E(new_ze), u_gen));
    };
  | (
      ShiftLeft | ShiftRight,
      CursorE(
        Staging(_),
        Parenthesized(_) | Inj(_) | Lam(_, _, _, _) | Case(_, _, _, _),
      ),
    ) =>
    // line shifting is handled at block level
    CantShift
  /* Movement */
  | (MoveTo(path), _) =>
    let e = ZExp.erase_zoperand(ze);
    switch (CursorPath.follow_exp(path, e)) {
    | Some(ze') => Succeeded((E(ze'), u_gen))
    | None => Failed
    };
  | (MoveToBefore(steps), _) =>
    let e = ZExp.erase_zoperand(ze);
    switch (CursorPath.follow_exp_and_place_before(steps, e)) {
    | Some(ze') => Succeeded((E(ze'), u_gen))
    | None => Failed
    };
  | (MoveToPrevHole, _) =>
    switch (CursorPath.prev_hole_steps(CursorPath.holes_ze(ze, []))) {
    | None => Failed
    | Some(path) =>
      ana_perform_exp(~ci, ctx, MoveTo(path), (ze, u_gen), ty)
    }
  | (MoveToNextHole, _) =>
    switch (CursorPath.next_hole_steps(CursorPath.holes_ze(ze, []))) {
    | None => Failed
    | Some(path) =>
      ana_perform_exp(~ci, ctx, MoveTo(path), (ze, u_gen), ty)
    }
  | (MoveLeft, _) =>
    ZExp.move_cursor_left_zoperand(ze)
    |> Opt.map_default(~default=CursorEscaped(Before), ze =>
         Succeeded((ZExp.E(ze), u_gen))
       )
  | (MoveRight, _) =>
    ZExp.move_cursor_right_zoperand(ze)
    |> Opt.map_default(~default=CursorEscaped(After), ze =>
         Succeeded((ZExp.E(ze), u_gen))
       )
  /* Backspace & Delete */
  | (Backspace, _) when ZExp.is_before_zoperand(ze) => CursorEscaped(Before)
  | (Delete, _) when ZExp.is_after_zoperand(ze) => CursorEscaped(After)
  | (Backspace, CursorE(_, EmptyHole(_) as e)) =>
    ZExp.is_after_zoperand(ze)
      ? Succeeded((E(ZExp.place_before_operand(e)), u_gen))
      : CursorEscaped(Before)
  | (Delete, CursorE(_, EmptyHole(_) as e)) =>
    ZExp.is_before_zoperand(ze)
      ? Succeeded((E(ZExp.place_after_operand(e)), u_gen))
      : CursorEscaped(After)
  | (
      Backspace | Delete,
      CursorE(OnText(_), Var(_, _, _) | NumLit(_, _) | BoolLit(_, _)),
    )
  | (Backspace | Delete, CursorE(OnDelim(_, _), ListNil(_))) =>
    let (ze, u_gen) = ZExp.new_EmptyHole(u_gen);
    Succeeded((E(ze), u_gen));
  /* ( _ <|)   ==>   ( _| ) */
  /* ... + [k-1] <|+ [k] + ...   ==>   ... + [k-1]| + [k] + ... */
  | (
      Backspace,
      CursorE(
        OnDelim(_, Before),
        Lam(_, _, _, _) | Inj(_, _, _) | Case(_, _, _, _) | Parenthesized(_) |
        OpSeq(_, _),
      ),
    ) =>
    ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
  /* (|> _ )   ==>   ( |_ ) */
  /* ... + [k-1] +|> [k] + ...   ==>   ... + [k-1] + |[k] + ... */
  | (
      Delete,
      CursorE(
        OnDelim(_, After),
        Lam(_, _, _, _) | Inj(_, _, _) | Case(_, _, _, _) | Parenthesized(_) |
        OpSeq(_, _),
      ),
    ) =>
    ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
  /* Delete before delimiter == Backspace after delimiter */
  | (
      Delete,
      CursorE(
        OnDelim(k, Before),
        (
          Lam(_, _, _, _) | Inj(_, _, _) | Case(_, _, _, _) |
          Parenthesized(_) |
          OpSeq(_, _)
        ) as e,
      ),
    ) =>
    ana_perform_exp(
      ~ci,
      ctx,
      Backspace,
      (CursorE(OnDelim(k, After), e), u_gen),
      ty,
    )
  /* \x :<| Num . x + 1   ==>   \x| . x + 1 */
  | (Backspace, CursorE(OnDelim(1, After), Lam(_, p, Some(_), block))) =>
    switch (HTyp.matched_arrow(ty)) {
    | None => Failed
    | Some((ty1, ty2)) =>
      let (p, ctx, u_gen) = Statics.ana_fix_holes_pat(ctx, u_gen, p, ty1);
      let (block, u_gen) =
        Statics.ana_fix_holes_block(ctx, u_gen, block, ty2);
      let ze = ZExp.LamZP(NotInHole, ZPat.place_after(p), None, block);
      Succeeded((E(ze), u_gen));
    }
  | (
      Backspace,
      CursorE(
        OnDelim(k, After),
        (
          Lam(_, _, _, _) | Inj(_, _, _) | Case(_, _, _, _) |
          Parenthesized(_)
        ) as e,
      ),
    ) =>
    Succeeded((E(CursorE(Staging(k), e)), u_gen))
  | (
      Backspace | Delete,
      CursorE(Staging(k), Parenthesized(Block(lines, e) as body)),
    ) =>
    let (result, u_gen) =
      switch (ci.frame, lines, e, e |> UHExp.bidelimited) {
      | (ExpFrame(_, None, _), _, _, _)
      | (_, _, OpSeq(_, _), _)
      | (ExpFrame(_, Some(_), _), [], _, true) => (
          body
          |> (
            switch (k) {
            | 0 => ZExp.place_before_block
            | _one => ZExp.place_after_block
            }
          ),
          u_gen,
        )
      | (_exp_frame_some, _, _, _) =>
        let (hole, u_gen) = u_gen |> ZExp.new_EmptyHole;
        (hole |> ZExp.wrap_in_block, u_gen);
      };
    Succeeded((B(result), u_gen));
  | (Backspace | Delete, CursorE(Staging(k), Case(_, scrut, _, _))) =>
    let result =
      scrut
      |> (
        switch (k) {
        | 0 => ZExp.place_before_block
        | _one => ZExp.place_after_block
        }
      );
    let (result, u_gen) =
      Statics.ana_fix_holes_zblock(ctx, u_gen, result, ty);
    Succeeded((B(result), u_gen));
  | (Backspace | Delete, CursorE(Staging(k), Inj(_, _, body))) =>
    let result =
      body
      |> (
        switch (k) {
        | 0 => ZExp.place_before_block
        | _one => ZExp.place_after_block
        }
      );
    let (result, u_gen) =
      Statics.ana_fix_holes_zblock(ctx, u_gen, result, ty);
    Succeeded((B(result), u_gen));
  | (Backspace | Delete, CursorE(Staging(k), Lam(_, _, _, body))) =>
    let result =
      body
      |> (
        switch (k) {
        | 0 => ZExp.place_before_block
        | _one => ZExp.place_after_block
        }
      );
    let (result, u_gen) =
      Statics.ana_fix_holes_zblock(ctx, u_gen, result, ty);
    Succeeded((B(result), u_gen));
  /* TODO consider deletion of type ascription on case */
  | (Backspace, CaseZR(_, _, (_, CursorR(OnDelim(_, Before), _), _), _)) =>
    ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
  | (Delete, CaseZR(_, _, (_, CursorR(OnDelim(_, After), _), _), _)) =>
    ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
  // Delete before delim == Backspace after delim
  | (
      Delete,
      CaseZR(
        err,
        scrut,
        (prefix, CursorR(OnDelim(k, Before), rule), suffix),
        ann,
      ),
    ) =>
    ana_perform_exp(
      ~ci=ci |> CursorInfo.update_position(OnDelim(k, After)),
      ctx,
      Backspace,
      (
        ZExp.CaseZR(
          err,
          scrut,
          (prefix, CursorR(OnDelim(k, After), rule), suffix),
          ann,
        ),
        u_gen,
      ),
      ty,
    )
  | (
      Backspace,
      CaseZR(
        err,
        scrut,
        (prefix, CursorR(OnDelim(k, After), rule), suffix),
        ann,
      ),
    ) =>
    Succeeded((
      E(
        CaseZR(
          err,
          scrut,
          (prefix, CursorR(Staging(k), rule), suffix),
          ann,
        ),
      ),
      u_gen,
    ))
  | (
      Backspace | Delete,
      CaseZR(_, scrut, (prefix, CursorR(Staging(_), _), suffix), ann),
    ) =>
    switch (suffix, prefix |> split_last) {
    | ([], None) =>
      let (zrule, u_gen) = ZExp.empty_zrule(u_gen);
      Succeeded((
        E(CaseZR(NotInHole, scrut, ([], zrule, []), ann)),
        u_gen,
      ));
    | ([first, ...rest], _) =>
      let zrule = ZExp.place_before_rule(first);
      let ze = ZExp.CaseZR(NotInHole, scrut, (prefix, zrule, rest), ann);
      Succeeded((E(ze), u_gen));
    | (_, Some((prefix_prefix, prefix_last))) =>
      let zrule = ZExp.place_after_rule(prefix_last);
      let ze =
        ZExp.CaseZR(NotInHole, scrut, (prefix_prefix, zrule, suffix), ann);
      Succeeded((E(ze), u_gen));
    }
  /* ... + [k-1] +<| [k] + ... */
  | (Backspace, CursorE(OnDelim(k, After), OpSeq(_, seq))) =>
    /* validity check at top of switch statement ensures
     * that op between [k-1] and [k] is not Space */
    switch (Seq.split(k - 1, seq), Seq.split(k, seq)) {
    /* invalid cursor position */
    | (None, _)
    | (_, None) => Failed
    /* ... + [k-1] +<| _ + ... */
    | (_, Some((EmptyHole(_), surround))) =>
      switch (surround) {
      /* invalid */
      | EmptyPrefix(_) => Failed
      /* ... + [k-1] +<| _   ==>   ... + [k-1]| */
      | EmptySuffix(prefix) =>
        let ze: ZExp.t =
          switch (prefix) {
          | OperandPrefix(e, _) => ZExp.place_after_operand(e)
          | SeqPrefix(seq, _) =>
            let skel = Associator.associate_exp(seq);
            ZExp.place_after_operand(OpSeq(skel, seq));
          };
        let (ze, u_gen) =
          Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, ze, ty);
        Succeeded((E(ze), u_gen));
      /* ... + [k-1] +<| _ + ...   ==>   ... + [k-1]| + ... */
      | BothNonEmpty(prefix, suffix) =>
        let (ze0: ZExp.t, surround: ZExp.opseq_surround) =
          switch (prefix) {
          | OperandPrefix(e, _) => (
              ZExp.place_after_operand(e),
              EmptyPrefix(suffix),
            )
          | SeqPrefix(ExpOpExp(e1, op, e2), _) => (
              ZExp.place_after_operand(e2),
              BothNonEmpty(OperandPrefix(e1, op), suffix),
            )
          | SeqPrefix(SeqOpExp(seq, op, e), _) => (
              ZExp.place_after_operand(e),
              BothNonEmpty(SeqPrefix(seq, op), suffix),
            )
          };
        let skel =
          Associator.associate_exp(
            Seq.t_of_operand_and_surround(
              ZExp.erase_zoperand(ze0),
              surround,
            ),
          );
        let ze = ZExp.OpSeqZ(skel, ze0, surround);
        let (ze, u_gen) =
          Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, ze, ty);
        Succeeded((E(ze), u_gen));
      }
    /* ... + _ +<| [k] + ... */
    | (Some((EmptyHole(_), surround)), _) =>
      switch (surround) {
      /* invalid */
      | EmptySuffix(_) => Failed
      /* _ +<| [k] + ...   ==>   |[k] + ... */
      | EmptyPrefix(suffix) =>
        let ze: ZExp.t =
          switch (suffix) {
          | OperandSuffix(_, e) => ZExp.place_before_operand(e)
          | SeqSuffix(_, seq) =>
            let skel = Associator.associate_exp(seq);
            ZExp.place_before_operand(OpSeq(skel, seq));
          };
        let (ze, u_gen) =
          Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, ze, ty);
        Succeeded((E(ze), u_gen));
      /* ... + [k-2] + _ +<| [k] + ...   ==>   ... + [k-2] +| [k] + ... */
      | BothNonEmpty(prefix, suffix) =>
        let seq =
          switch (suffix) {
          | OperandSuffix(_, e) => Seq.t_of_prefix_and_last(prefix, e)
          | SeqSuffix(_, seq) => Seq.t_of_prefix_and_seq(prefix, seq)
          };
        let skel = Associator.associate_exp(seq);
        let ze = ZExp.CursorE(OnDelim(k - 1, After), OpSeq(skel, seq));
        let (ze, u_gen) =
          Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, ze, ty);
        Succeeded((E(ze), u_gen));
      }
    /* ... + [k-1] +<| [k] + ...   ==>   ... + [k-1]| [k] + ... */
    | (Some((e0, surround)), _) =>
      switch (Seq.replace_following_op(surround, UHExp.Space)) {
      | None => Failed /* invalid */
      | Some(surround) =>
        let (ze, u_gen) =
          make_and_ana_OpSeqZ(
            ctx,
            u_gen,
            ZExp.place_after_operand(e0),
            surround,
            ty,
          );
        Succeeded((E(ze), u_gen));
      }
    }
  /* ... + [k-1]  <|_ + [k+1] + ...  ==>   ... + [k-1]| + [k+1] + ... */
  | (
      Backspace,
      OpSeqZ(
        _,
        CursorE(_, EmptyHole(_)) as ze0,
        (
          EmptySuffix(OperandPrefix(_, Space) | SeqPrefix(_, Space)) |
          BothNonEmpty(OperandPrefix(_, Space) | SeqPrefix(_, Space), _)
        ) as surround,
      ),
    )
      when ZExp.is_before_zoperand(ze0) =>
    switch (surround) {
    | EmptyPrefix(_) => CursorEscaped(Before) /* should never happen */
    | EmptySuffix(prefix) =>
      let e: UHExp.t =
        switch (prefix) {
        | OperandPrefix(e1, _space) => e1
        | SeqPrefix(seq, _space) =>
          let skel = Associator.associate_exp(seq);
          OpSeq(skel, seq);
        };
      let (ze, u_gen) =
        Statics.Exp.ana_fix_holes_zoperand(
          ctx,
          u_gen,
          ZExp.place_after_operand(e),
          ty,
        );
      Succeeded((E(ze), u_gen));
    | BothNonEmpty(prefix, suffix) =>
      switch (prefix) {
      | OperandPrefix(e1, _space) =>
        let ze1 = ZExp.place_after_operand(e1);
        let seq = Seq.t_of_first_and_suffix(e1, suffix);
        let skel = Associator.associate_exp(seq);
        let ze = ZExp.OpSeqZ(skel, ze1, EmptyPrefix(suffix));
        let (ze, u_gen) =
          Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, ze, ty);
        Succeeded((E(ze), u_gen));
      | SeqPrefix(seq, _space) =>
        let (prefix: ZExp.prefix, e0) =
          switch (seq) {
          | ExpOpExp(e1, op, e2) => (OperandPrefix(e1, op), e2)
          | SeqOpExp(seq, op, e1) => (SeqPrefix(seq, op), e1)
          };
        let ze0 = ZExp.place_after_operand(e0);
        let surround = Seq.BothNonEmpty(prefix, suffix);
        let seq = Seq.t_of_operand_and_surround(e0, surround);
        let skel = Associator.associate_exp(seq);
        let ze = ZExp.OpSeqZ(skel, ze0, surround);
        let (ze, u_gen) =
          Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, ze, ty);
        Succeeded((E(ze), u_gen));
      }
    }
  /* ... + [k-1] + _|>  [k+1] + ...  ==>   ... + [k-1] + |[k+1] + ... */
  | (
      Delete,
      OpSeqZ(
        _,
        CursorE(_, EmptyHole(_)) as ze0,
        (
          EmptyPrefix(OperandSuffix(Space, _) | SeqSuffix(Space, _)) |
          BothNonEmpty(_, OperandSuffix(Space, _) | SeqSuffix(Space, _))
        ) as surround,
      ),
    )
      when ZExp.is_after_zoperand(ze0) =>
    switch (surround) {
    | EmptySuffix(_) => CursorEscaped(After) /* should never happen */
    | EmptyPrefix(suffix) =>
      let e =
        switch (suffix) {
        | OperandSuffix(_space, e1) => e1
        | SeqSuffix(_space, seq) =>
          let skel = Associator.associate_exp(seq);
          OpSeq(skel, seq);
        };
      let (ze, u_gen) =
        Statics.Exp.ana_fix_holes_zoperand(
          ctx,
          u_gen,
          ZExp.place_before_operand(e),
          ty,
        );
      Succeeded((E(ze), u_gen));
    | BothNonEmpty(prefix, suffix) =>
      switch (suffix) {
      | OperandSuffix(_space, e1) =>
        let ze1 = ZExp.place_before_operand(e1);
        let seq = Seq.t_of_prefix_and_last(prefix, e1);
        let skel = Associator.associate_exp(seq);
        let ze = ZExp.OpSeqZ(skel, ze1, EmptySuffix(prefix));
        let (ze, u_gen) =
          Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, ze, ty);
        Succeeded((E(ze), u_gen));
      | SeqSuffix(_space, seq) =>
        let (e0, suffix: ZExp.suffix) =
          switch (seq) {
          | ExpOpExp(e1, op, e2) => (e1, OperandSuffix(op, e2))
          | SeqOpExp(seq, op, e1) => (e1, SeqSuffix(op, seq))
          };
        let ze0 = ZExp.place_before_operand(e0);
        let surround = Seq.BothNonEmpty(prefix, suffix);
        let seq = Seq.t_of_operand_and_surround(e0, surround);
        let skel = Associator.associate_exp(seq);
        let ze = ZExp.OpSeqZ(skel, ze0, surround);
        let (ze, u_gen) =
          Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, ze, ty);
        Succeeded((E(ze), u_gen));
      }
    }
  /* Construction */
  | (Construct(SLine), CursorE(Staging(k), e)) =>
    let (new_ze, u_gen) =
      Statics.Exp.ana_fix_holes_zoperand(
        ctx,
        u_gen,
        CursorE(OnDelim(k, After), e),
        ty,
      );
    Succeeded((E(new_ze), u_gen));
  | (Construct(_), CursorE(Staging(_), _)) => Failed
  | (
      Construct(SLine),
      CaseZR(err, scrut, (prefix, CursorR(Staging(k), rule), suffix), ann),
    ) =>
    let (new_ze, u_gen) =
      Statics.Exp.ana_fix_holes_zoperand(
        ctx,
        u_gen,
        CaseZR(
          err,
          scrut,
          (prefix, CursorR(OnDelim(k, After), rule), suffix),
          ann,
        ),
        ty,
      );
    Succeeded((E(new_ze), u_gen));
  | (
      Construct(SOp(SSpace)),
      CursorE(OnDelim(_, After), _) |
      CaseZR(_, _, (_, CursorR(OnDelim(_, After), _), _), _),
    )
      when !ZExp.is_after_zoperand(ze) =>
    ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
  | (Construct(_) as a, CursorE(OnDelim(_, side), _))
      when !ZExp.is_before_zoperand(ze) && !ZExp.is_after_zoperand(ze) =>
    let move_then_perform = move_action =>
      switch (ana_perform_exp(~ci, ctx, move_action, edit_state, ty)) {
      | Failed
      | CantShift
      | CursorEscaped(_)
      | Succeeded((B(_), _)) => assert(false)
      | Succeeded((E(ze), u_gen)) =>
        CursorInfo.ana_cursor_info(
          ~frame=ci |> CursorInfo.force_get_exp_frame,
          ctx,
          ze,
          ty,
        )
        |> Opt.map_default(~default=Failed, ci =>
             ana_perform_exp(~ci, ctx, a, (ze, u_gen), ty)
           )
      };
    switch (side) {
    | Before => move_then_perform(MoveLeft)
    | After => move_then_perform(MoveRight)
    };
  | (Construct(SLine), CursorE(_, _))
  | (Construct(SLet), CursorE(_, _)) =>
    /* handled at block or line level */
    Failed
  | (
      Construct(SLine),
      CaseZR(err, e1, (prefix, RuleZP(zp, re), suffix), ann),
    )
      when ZPat.is_before(zp) =>
    let (zrule, u_gen) = ZExp.empty_zrule(u_gen);
    let prev_rule = UHExp.Rule(ZPat.erase(zp), re);
    let suffix = [prev_rule, ...suffix];
    let ze = ZExp.CaseZR(err, e1, (prefix, zrule, suffix), ann);
    Succeeded((E(ze), u_gen));
  | (
      Construct(SLine),
      CaseZR(err, e1, (prefix, RuleZE(_, ze) as zrule, suffix), ann),
    )
      when ZExp.is_after_zblock(ze) =>
    let prev_rule = ZExp.erase_zrule(zrule);
    let (zrule, u_gen) = ZExp.empty_zrule(u_gen);
    let prefix = prefix @ [prev_rule];
    let ze = ZExp.CaseZR(err, e1, (prefix, zrule, suffix), ann);
    Succeeded((E(ze), u_gen));
  | (
      Construct(SLine),
      CaseZR(err, e1, (prefix, RuleZP(zp, _) as zrule, suffix), ann),
    )
      when ZPat.is_after(zp) =>
    let prev_rule = ZExp.erase_zrule(zrule);
    let (zrule, u_gen) = ZExp.empty_zrule(u_gen);
    let prefix = prefix @ [prev_rule];
    let ze = ZExp.CaseZR(err, e1, (prefix, zrule, suffix), ann);
    Succeeded((E(ze), u_gen));
  | (Construct(SCase), ze1) when ZExp.is_before_zoperand(ze1) =>
    let e1 = ZExp.erase_zoperand(ze1);
    let (ze, u_gen) =
      switch (e1) {
      | EmptyHole(_) =>
        let (rule, u_gen) = UHExp.empty_rule(u_gen);
        (
          ZExp.CaseZE(NotInHole, ZExp.wrap_in_block(ze1), [rule], None),
          u_gen,
        );
      | _ =>
        let (zrule, u_gen) = ZExp.empty_zrule(u_gen);
        let zrules = ZList.singleton(zrule);
        (
          ZExp.CaseZR(NotInHole, UHExp.wrap_in_block(e1), zrules, None),
          u_gen,
        );
      };
    Succeeded((E(ze), u_gen));
  | (Construct(SCase), CursorE(_, _)) => Failed
  | (Construct(SParenthesized), CursorE(_, _)) =>
    Succeeded((E(ParenthesizedZ(ZExp.wrap_in_block(ze))), u_gen))
  | (Construct(SAsc), LamZP(err, zp, None, e1)) =>
    let ze = ZExp.LamZA(err, ZPat.erase(zp), ZTyp.place_before(Hole), e1);
    Succeeded((E(ze), u_gen));
  | (Construct(SAsc), LamZP(err, zp, Some(uty1), e1)) =>
    /* just move the cursor over if there is already an ascription */
    let ze = ZExp.LamZA(err, ZPat.erase(zp), ZTyp.place_before(uty1), e1);
    Succeeded((E(ze), u_gen));
  | (Construct(SAsc), CursorE(_, Case(_, e1, rules, None))) =>
    let ze = ZExp.CaseZA(NotInHole, e1, rules, ZTyp.place_before(Hole));
    Succeeded((E(ze), u_gen));
  | (Construct(SAsc), CursorE(_, Case(_, e1, rules, Some(uty)))) =>
    /* just move the cursor over if there is already an ascription */
    let ze = ZExp.CaseZA(NotInHole, e1, rules, ZTyp.place_before(uty));
    Succeeded((E(ze), u_gen));
  | (Construct(SAsc), CursorE(_, _)) => Failed
  | (Construct(SLam), CursorE(_, _)) =>
    let e = ZExp.erase_zoperand(ze);
    switch (HTyp.matched_arrow(ty)) {
    | Some((_, ty2)) =>
      let (e, u_gen) = Statics.ana_fix_holes_exp(ctx, u_gen, e, ty2);
      let (zp, u_gen) = ZPat.new_EmptyHole(u_gen);
      let ze = ZExp.LamZP(NotInHole, zp, None, UHExp.wrap_in_block(e));
      Succeeded((E(ze), u_gen));
    | None =>
      let (e, _, u_gen) = Statics.syn_fix_holes_exp(ctx, u_gen, e);
      let (zp, u_gen) = ZPat.new_EmptyHole(u_gen);
      let (u, u_gen) = MetaVarGen.next(u_gen);
      let ze =
        ZExp.LamZP(
          InHole(TypeInconsistent, u),
          zp,
          None,
          UHExp.wrap_in_block(e),
        );
      Succeeded((E(ze), u_gen));
    };
  | (Construct(SInj(side)), CursorE(_, _) as ze1) =>
    switch (HTyp.matched_sum(ty)) {
    | Some((tyL, tyR)) =>
      let ty1 = InjSide.pick(side, tyL, tyR);
      let (ze1, u_gen) =
        Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, ze1, ty1);
      let ze = ZExp.InjZ(NotInHole, side, ZExp.wrap_in_block(ze1));
      Succeeded((E(ze), u_gen));
    | None =>
      let (ze1, _, u_gen) =
        Statics.Exp.syn_fix_holes_zoperand(ctx, u_gen, ze1);
      let (u, u_gen) = MetaVarGen.next(u_gen);
      let ze =
        ZExp.InjZ(
          InHole(TypeInconsistent, u),
          side,
          ZExp.wrap_in_block(ze1),
        );
      Succeeded((E(ze), u_gen));
    }
  | (
      Construct(SOp(SSpace)),
      OpSeqZ(_, CursorE(OnDelim(_, After), _) as ze0, _),
    )
      when !ZExp.is_after_zoperand(ze0) =>
    ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
  | (Construct(SOp(os)), OpSeqZ(_, ze0, surround))
      when ZExp.is_after_zoperand(ze0) =>
    switch (exp_op_of(os)) {
    | None => Failed
    | Some(op) =>
      let (ze, u_gen) =
        abs_perform_Construct_SOp_After_surround(
          UHExp.new_EmptyHole,
          (ctx, u_gen, cursor, seq) =>
            make_and_ana_OpSeq(ctx, u_gen, cursor, seq, ty),
          (ctx, u_gen, ze, surround) =>
            make_and_ana_OpSeqZ(ctx, u_gen, ze, surround, ty),
          UHExp.is_Space,
          UHExp.Space,
          ZExp.place_before_operand,
          ctx,
          u_gen,
          ZExp.erase_zoperand(ze0),
          op,
          surround,
        );
      Succeeded((E(ze), u_gen));
    }
  | (Construct(SOp(os)), OpSeqZ(_, ze0, surround))
      when ZExp.is_before_zoperand(ze0) =>
    switch (exp_op_of(os)) {
    | None => Failed
    | Some(op) =>
      let (ze, u_gen) =
        abs_perform_Construct_SOp_Before_surround(
          UHExp.new_EmptyHole,
          (ctx, u_gen, cursor, seq) =>
            make_and_ana_OpSeq(ctx, u_gen, cursor, seq, ty),
          (ctx, u_gen, ze, surround) =>
            make_and_ana_OpSeqZ(ctx, u_gen, ze, surround, ty),
          UHExp.is_Space,
          UHExp.Space,
          ZExp.place_before_operand,
          ctx,
          u_gen,
          ze0 |> ZExp.erase_zoperand,
          op,
          surround,
        );
      Succeeded((E(ze), u_gen));
    }
  | (Construct(SOp(os)), CursorE(_, _)) =>
    switch (exp_op_of(os)) {
    | None => Failed
    | Some(op) =>
      if (ZExp.is_before_zoperand(ze)) {
        let (ze, u_gen) =
          abs_perform_Construct_SOp_Before(
            UHExp.bidelimit,
            UHExp.new_EmptyHole,
            (ctx, u_gen, cursor, seq) =>
              make_and_ana_OpSeq(ctx, u_gen, cursor, seq, ty),
            (ctx, u_gen, ze, surround) =>
              make_and_ana_OpSeqZ(ctx, u_gen, ze, surround, ty),
            UHExp.is_Space,
            ZExp.place_before_operand,
            ctx,
            u_gen,
            ZExp.erase_zoperand(ze),
            op,
          );
        Succeeded((E(ze), u_gen));
      } else if (ZExp.is_after_zoperand(ze)) {
        let (ze, u_gen) =
          abs_perform_Construct_SOp_After(
            UHExp.bidelimit,
            UHExp.new_EmptyHole,
            (ctx, u_gen, cursor, seq) =>
              make_and_ana_OpSeq(ctx, u_gen, cursor, seq, ty),
            (ctx, u_gen, ze, surround) =>
              make_and_ana_OpSeqZ(ctx, u_gen, ze, surround, ty),
            UHExp.is_Space,
            ZExp.place_before_operand,
            ctx,
            u_gen,
            ZExp.erase_zoperand(ze),
            op,
          );
        Succeeded((E(ze), u_gen));
      } else {
        Failed;
      }
    }
  /* Zipper Cases */
  | (_, ParenthesizedZ(zblock)) =>
    switch (ana_perform_block(~ci, ctx, a, (zblock, u_gen), ty)) {
    | Failed => Failed
    | CantShift => CantShift
    | CursorEscaped(Before) =>
      ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
    | CursorEscaped(After) =>
      ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
    | Succeeded((zblock, u_gen)) =>
      Succeeded((E(ParenthesizedZ(zblock)), u_gen))
    }
  | (_, LamZP(err, zp, ann, block)) =>
    switch (HTyp.matched_arrow(ty)) {
    | None => Failed
    | Some((ty1_given, ty2)) =>
      let ty1 =
        switch (ann) {
        | Some(uty1) => UHTyp.expand(uty1)
        | None => ty1_given
        };
      switch (Pat.ana_perform(ctx, u_gen, a, zp, ty1)) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
      | CursorEscaped(After) =>
        ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
      | Succeeded((zp, ctx, u_gen)) =>
        let (block, u_gen) =
          Statics.ana_fix_holes_block(ctx, u_gen, block, ty2);
        let ze = ZExp.LamZP(err, zp, ann, block);
        Succeeded((E(ze), u_gen));
      };
    }
  | (_, LamZA(_, p, zann, block)) =>
    switch (HTyp.matched_arrow(ty)) {
    | None => Failed
    | Some((ty1_given, ty2)) =>
      switch (perform_ty(a, zann)) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
      | CursorEscaped(After) =>
        ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
      | Succeeded(zann) =>
        let ty1 = UHTyp.expand(ZTyp.erase(zann));
        HTyp.consistent(ty1, ty1_given)
          ? {
            let (p, ctx, u_gen) =
              Statics.ana_fix_holes_pat(ctx, u_gen, p, ty1);
            let (block, u_gen) =
              Statics.ana_fix_holes_block(ctx, u_gen, block, ty2);
            let ze = ZExp.LamZA(NotInHole, p, zann, block);
            Succeeded((E(ze), u_gen));
          }
          : {
            let (p, ctx, u_gen) =
              Statics.ana_fix_holes_pat(ctx, u_gen, p, ty1);
            let (block, _, u_gen) =
              Statics.syn_fix_holes_block(ctx, u_gen, block);
            let (u, u_gen) = MetaVarGen.next(u_gen);
            let ze = ZExp.LamZA(InHole(TypeInconsistent, u), p, zann, block);
            Succeeded((E(ze), u_gen));
          };
      }
    }
  | (_, LamZE(err, p, ann, zblock)) =>
    switch (HTyp.matched_arrow(ty)) {
    | None => Failed
    | Some((ty1_given, ty2)) =>
      let ty1 =
        switch (ann) {
        | Some(uty1) => UHTyp.expand(uty1)
        | None => ty1_given
        };
      switch (Statics.Pat.ana(ctx, p, ty1)) {
      | None => Failed
      | Some(ctx_body) =>
        switch (ana_perform_block(~ci, ctx_body, a, (zblock, u_gen), ty2)) {
        | Failed => Failed
        | CantShift => CantShift
        | CursorEscaped(Before) =>
          ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
        | CursorEscaped(After) =>
          ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
        | Succeeded((zblock, u_gen)) =>
          let ze = ZExp.LamZE(err, p, ann, zblock);
          Succeeded((E(ze), u_gen));
        }
      };
    }
  | (_, InjZ(err, side, zblock)) =>
    switch (HTyp.matched_sum(ty)) {
    | None => Failed
    | Some((ty1, ty2)) =>
      let picked = InjSide.pick(side, ty1, ty2);
      switch (ana_perform_block(~ci, ctx, a, (zblock, u_gen), picked)) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
      | CursorEscaped(After) =>
        ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
      | Succeeded((zblock, u_gen)) =>
        Succeeded((E(InjZ(err, side, zblock)), u_gen))
      };
    }
  | (_, CaseZE(_, zblock, rules, ann)) =>
    switch (Statics.syn_block(ctx, ZExp.erase_zblock(zblock))) {
    | None => Failed
    | Some(ty1) =>
      switch (syn_perform_block(~ci, ctx, a, (zblock, ty1, u_gen))) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
      | CursorEscaped(After) =>
        ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
      | Succeeded((zblock, ty1, u_gen)) =>
        let (rules, u_gen) =
          Statics.ana_fix_holes_rules(ctx, u_gen, rules, ty1, ty);
        let ze = ZExp.CaseZE(NotInHole, zblock, rules, ann);
        Succeeded((E(ze), u_gen));
      }
    }
  | (_, CaseZR(_, block, zrules, ann)) =>
    switch (Statics.syn_block(ctx, block)) {
    | None => Failed
    | Some(ty1) =>
      switch (ZList.prj_z(zrules)) {
      | CursorR(_, _) => Failed /* handled in earlier case */
      | RuleZP(zp, clause) =>
        switch (Pat.ana_perform(ctx, u_gen, a, zp, ty1)) {
        | Failed => Failed
        | CantShift => CantShift
        | CursorEscaped(Before) =>
          ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
        | CursorEscaped(After) =>
          ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
        | Succeeded((zp, ctx, u_gen)) =>
          let (clause, u_gen) =
            Statics.ana_fix_holes_block(ctx, u_gen, clause, ty);
          let zrule = ZExp.RuleZP(zp, clause);
          let ze =
            ZExp.CaseZR(
              NotInHole,
              block,
              ZList.replace_z(zrules, zrule),
              ann,
            );
          Succeeded((E(ze), u_gen));
        }
      | RuleZE(p, zclause) =>
        switch (Statics.Pat.ana(ctx, p, ty1)) {
        | None => Failed
        | Some(ctx) =>
          switch (ana_perform_block(~ci, ctx, a, (zclause, u_gen), ty)) {
          | Failed => Failed
          | CantShift => CantShift
          | CursorEscaped(Before) =>
            ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
          | CursorEscaped(After) =>
            ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
          | Succeeded((zclause, u_gen)) =>
            let zrule = ZExp.RuleZE(p, zclause);
            let ze =
              ZExp.CaseZR(
                NotInHole,
                block,
                ZList.replace_z(zrules, zrule),
                ann,
              );
            Succeeded((E(ze), u_gen));
          }
        }
      }
    }
  | (_, CaseZA(_, block, rules, zann)) =>
    switch (Statics.syn_block(ctx, block)) {
    | None => Failed
    | Some(ty1) =>
      switch (perform_ty(a, zann)) {
      | Failed => Failed
      | CantShift => CantShift
      | CursorEscaped(Before) =>
        ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
      | CursorEscaped(After) =>
        ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
      | Succeeded(zann) =>
        let ty2 = UHTyp.expand(ZTyp.erase(zann));
        let (rules, u_gen) =
          Statics.ana_fix_holes_rules(ctx, u_gen, rules, ty1, ty2);
        let ze = ZExp.CaseZA(NotInHole, block, rules, zann);
        let (ze, u_gen) =
          Statics.Exp.ana_fix_holes_zoperand(ctx, u_gen, ze, ty);
        Succeeded((E(ze), u_gen));
      }
    }
  | (_, OpSeqZ(_, ze0, surround)) =>
    let i = Seq.surround_prefix_length(surround);
    switch (ZExp.erase_zoperand(ze)) {
    | OpSeq(skel, seq) =>
      switch (Statics.ana_skel(ctx, skel, seq, ty, Some(i))) {
      | Some(Some(mode)) =>
        switch (mode) {
        | Statics.AnalyzedAgainst(ty0) =>
          switch (ana_perform_exp(~ci, ctx, a, (ze0, u_gen), ty0)) {
          | Failed => Failed
          | CantShift => CantShift
          | CursorEscaped(Before) =>
            ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
          | CursorEscaped(After) =>
            ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
          | Succeeded((ze_zb, u_gen)) =>
            let ze0 =
              switch (ze_zb) {
              | E(ze) => ZExp.bidelimit(ze)
              | B(zblock) =>
                switch (zblock) {
                | BlockZL(_, _)
                | BlockZE([_, ..._], _) => ParenthesizedZ(zblock)
                | BlockZE([], ze) => ze
                }
              };
            let (ze0, surround) = OpSeqUtil.Exp.resurround(ze0, surround);
            Succeeded((E(OpSeqZ(skel, ze0, surround)), u_gen));
          }
        | Statics.Synthesized(ty0) =>
          switch (syn_perform_exp(~ci, ctx, a, (ze0, ty0, u_gen))) {
          | Failed => Failed
          | CantShift => CantShift
          | CursorEscaped(Before) =>
            ana_perform_exp(~ci, ctx, MoveLeft, edit_state, ty)
          | CursorEscaped(After) =>
            ana_perform_exp(~ci, ctx, MoveRight, edit_state, ty)
          | Succeeded((ze_or_zblock, _, u_gen)) =>
            let ze0 =
              switch (ze_or_zblock) {
              | E(ze) => ZExp.bidelimit(ze)
              | B(zblock) =>
                switch (zblock) {
                | BlockZL(_, _)
                | BlockZE([_, ..._], _) => ParenthesizedZ(zblock)
                | BlockZE([], ze) => ze
                }
              };
            let (ze0, surround) = OpSeqUtil.Exp.resurround(ze0, surround);
            let (ze, u_gen) =
              make_and_ana_OpSeqZ(ctx, u_gen, ze0, surround, ty);
            Succeeded((E(ze), u_gen));
          }
        }
      | Some(_) => Failed /* should never happen */
      | None => Failed /* should never happen */
      }
    | _ => Failed /* should never happen */
    };
  /* Subsumption */
  | (UpdateApPalette(_), _)
  | (Construct(SApPalette(_)), _)
  | (Construct(SLine), _)
  | (Construct(SVar(_, _)), _)
  | (Construct(SNumLit(_, _)), _)
  | (Construct(SListNil), _)
  | (_, ApPaletteZ(_, _, _, _)) =>
    ana_perform_exp_subsume(~ci, ctx, a, (ze, u_gen), ty)
  /* Invalid actions at expression level */
  | (Construct(SNum), _)
  | (Construct(SBool), _)
  | (Construct(SList), _)
  | (Construct(SWild), _) => Failed
  }
and ana_perform_exp_subsume =
    (
      ~ci: CursorInfo.t,
      ctx: Contexts.t,
      a: t,
      (ze, u_gen): (ZExp.t, MetaVarGen.t),
      ty: HTyp.t,
    )
    : result((zexp_or_zblock, MetaVarGen.t)) =>
  switch (Statics.Exp.syn_operand(ctx, ZExp.erase_zoperand(ze))) {
  | None => Failed
  | Some(ty1) =>
    switch (syn_perform_exp(~ci, ctx, a, (ze, ty1, u_gen))) {
    | (Failed | CantShift | CursorEscaped(_)) as err => err
    | Succeeded((ze_zb, ty1, u_gen)) =>
      if (HTyp.consistent(ty, ty1)) {
        Succeeded((ze_zb, u_gen));
      } else {
        let (ze_zb, u_gen) = make_zexp_or_zblock_inconsistent(u_gen, ze_zb);
        Succeeded((ze_zb, u_gen));
      }
    }
  };

let can_perform =
    (
      ctx: Contexts.t,
      edit_state: (ZExp.zblock, HTyp.t, MetaVarGen.t),
      ci: CursorInfo.t,
      a: t,
    )
    : bool =>
  switch (a) {
  | Construct(SParenthesized) => true
  | Construct(SLine)
  | Construct(SLet)
  | Construct(SCase) =>
    switch (ci.node) {
    | Line(_) => true
    | Exp(_) => true
    | Rule(_) => false
    | Pat(_) => false
    | Typ(_) => false
    }
  | Construct(SInj(_)) =>
    switch (ci.node) {
    | Line(_) => true
    | Exp(_) => true
    | Rule(_) => false
    | Pat(_) => true
    | Typ(_) => false
    }
  | Construct(SListNil) =>
    switch (ci.node) {
    | Line(EmptyLine) => true
    | Line(ExpLine(EmptyHole(_))) => true
    | Line(_) => false
    | Exp(EmptyHole(_)) => true
    | Exp(_) => false
    | Pat(EmptyHole(_)) => true
    | Pat(_) => false
    | Typ(_) => false
    | Rule(_) => false
    }
  | Construct(SOp(SArrow))
  | Construct(SOp(SVBar))
  | Construct(SList) =>
    switch (ci.node) {
    | Typ(_) => true
    | Line(_)
    | Exp(_)
    | Rule(_)
    | Pat(_) => false
    }
  | Construct(SAsc)
  | Construct(SApPalette(_))
  | Construct(SLam)
  | Construct(SVar(_, _)) /* see can_enter_varchar below */
  | Construct(SWild)
  | Construct(SNumLit(_, _)) /* see can_enter_numeral below */
  | Construct(SOp(_))
  | Construct(SNum) /* TODO enrich cursor_info to allow simplifying these type cases */
  | Construct(SBool) /* TODO enrich cursor_info to allow simplifying these type cases */
  | MoveTo(_)
  | MoveToBefore(_)
  | MoveToNextHole
  | MoveToPrevHole
  | MoveLeft
  | MoveRight
  | UpdateApPalette(_)
  | Delete
  | Backspace
  | ShiftLeft
  | ShiftRight
  | ShiftUp
  | ShiftDown =>
    _TEST_PERFORM
      ? switch (syn_perform_block(~ci, ctx, a, edit_state)) {
        | Succeeded(_) => true
        | CantShift
        | CursorEscaped(_)
        | Failed => false
        }
      : true
  };

let can_enter_varchar = (ci: CursorInfo.t): bool =>
  switch (ci.node) {
  | Line(EmptyLine)
  | Line(ExpLine(EmptyHole(_)))
  | Exp(Var(_, _, _))
  | Exp(EmptyHole(_))
  | Exp(BoolLit(_, _))
  | Pat(Var(_, _, _))
  | Pat(EmptyHole(_))
  | Pat(BoolLit(_, _)) => true
  | Exp(NumLit(_, _))
  | Pat(NumLit(_, _)) =>
    switch (ci.position) {
    | OnText(_) => true
    | _ => false
    }
  | Line(_)
  | Exp(_)
  | Rule(_)
  | Pat(_)
  | Typ(_) => false
  };

let can_enter_numeral = (ci: CursorInfo.t): bool =>
  switch (ci.node) {
  | Line(EmptyLine)
  | Line(ExpLine(EmptyHole(_)))
  | Exp(NumLit(_, _))
  | Exp(EmptyHole(_))
  | Pat(NumLit(_, _))
  | Pat(EmptyHole(_)) => true
  | Line(_)
  | Exp(_)
  | Rule(_)
  | Pat(_)
  | Typ(_) => false
  };

let can_construct_palette = (ci: CursorInfo.t): bool =>
  switch (ci.node) {
  | Line(EmptyLine)
  | Line(ExpLine(EmptyHole(_)))
  | Exp(EmptyHole(_)) => true
  | _ => false
  };
