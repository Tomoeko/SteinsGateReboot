# Actual pool attachment for live motion packets

`MotionSpriteRecordReceiver` can now borrow the actual descriptor-derived `FrameHostPool` attached to its exact `SpriteRenderRecords`. Construction checks that identity before table allocation; Draw checks the live input producer before real record Begin. The existing explicit heap constructor retains its caller-fenced contract.

Each packet creates one `SpriteHostInputs` in a fixed optional cell. The pool copies the packet once into its owned physical heap, checks both allocation owners, and publishes an unforgeable borrow beside that same cell. Prior packet cells stay stable through the enclosing actual encoder frame. At real quiescence the receiver destroys each borrow before its input; the pool rejects recycle and close while retained inputs or real bound writers remain.

Correctable admission errors retain the existing outer encoder. A later unsupported event aborts that actual encoder, preserving its first cause, prior inputs and failed context until explicit quiescent retirement. CPU cleanup uses inactive records and actual heap ownership after Abort, without claiming healthy presentation metadata.

The bounded source proof uses the immutable pool owner, composed record/scissor state, actual selected rows and shared texture cache. It checks exact-owner rejection before registration, two retained packet cells, blocked recycle, post-mutation Abort retention and closed-pool admission before Begin. The first submitted offscreen frame supplies contrasting red/blue pixel comparisons. The window stays hidden; no drawable request or Present occurs.

This is a first-party lifetime connection over retained native pool/record/packet evidence. It adds no new native capture claim. Whole Display pre/post hook installation, native exception recovery, GPU resource registration, general masked/copy/filter events, timing, full Kurisu frames and playable startup remain open.
