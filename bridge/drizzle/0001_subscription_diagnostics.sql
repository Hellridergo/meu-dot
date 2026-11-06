CREATE TABLE `subscription_checks` (
	`name` text PRIMARY KEY NOT NULL,
	`at` integer NOT NULL,
	`stage` text NOT NULL,
	`http` integer
);
