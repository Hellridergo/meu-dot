ALTER TABLE cards ADD COLUMN delivered INTEGER;
--> statement-breakpoint
ALTER TABLE cards ADD COLUMN delivery_stage TEXT;
--> statement-breakpoint
ALTER TABLE cards ADD COLUMN recording_id TEXT;
--> statement-breakpoint
ALTER TABLE deliveries ADD COLUMN attempted INTEGER;
--> statement-breakpoint
ALTER TABLE deliveries ADD COLUMN sent INTEGER;
--> statement-breakpoint
ALTER TABLE deliveries ADD COLUMN http INTEGER;
