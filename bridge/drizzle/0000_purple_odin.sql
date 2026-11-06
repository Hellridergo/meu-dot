CREATE TABLE `cards` (
	`id` text PRIMARY KEY NOT NULL,
	`message` text NOT NULL,
	`decision` integer NOT NULL,
	`created` integer NOT NULL,
	`answer` text,
	`answered` integer
);
--> statement-breakpoint
CREATE TABLE `deliveries` (
	`sub` text NOT NULL,
	`event` text NOT NULL,
	`name` text NOT NULL,
	`data` text NOT NULL,
	`created` integer NOT NULL,
	`phase` text DEFAULT 'pending' NOT NULL,
	`tries` integer DEFAULT 0 NOT NULL,
	`due` integer DEFAULT 0 NOT NULL,
	`lease` text,
	PRIMARY KEY(`sub`, `event`)
);
--> statement-breakpoint
CREATE INDEX `delivery_due` ON `deliveries` (`phase`,`due`);--> statement-breakpoint
CREATE TABLE `mobile` (
	`id` integer PRIMARY KEY NOT NULL,
	`last_seen` integer NOT NULL,
	`client` text NOT NULL
);
--> statement-breakpoint
CREATE TABLE `subscriptions` (
	`id` text PRIMARY KEY NOT NULL,
	`name` text NOT NULL,
	`filter_id` text NOT NULL,
	`sealed` text NOT NULL,
	`created` integer NOT NULL,
	`expires` integer NOT NULL
);
--> statement-breakpoint
CREATE TABLE `voices` (
	`id` text PRIMARY KEY NOT NULL,
	`message` text NOT NULL,
	`seconds` real NOT NULL,
	`created` integer NOT NULL
);
